// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <stdexcept>

#include "common/common_funcs.h"
#include "tests/core/ipc_request.h"

namespace TestKernel {

namespace {

/// The two descriptor kinds this builder emits differ only in which count the header carries, so
/// one encoder serves both. Flags stay zero: that is the plain "map the pages" mode, and the HLE
/// receive path never maps anything anyway, it reads the client's own addresses.
[[nodiscard]] IPC::BufferDescriptorABW MakeDescriptor(u64 address, u64 size) {
    IPC::BufferDescriptorABW descriptor{};
    descriptor.size_bits_0_31 = static_cast<u32>(size);
    descriptor.address_bits_0_31 = static_cast<u32>(address);
    descriptor.size_bits_32_35.Assign(static_cast<u32>(size >> 32));
    descriptor.address_bits_32_35.Assign(static_cast<u32>(address >> 32));
    descriptor.address_bits_36_38.Assign(static_cast<u32>(address >> 36));
    return descriptor;
}

} // Anonymous namespace

RequestBuilder& RequestBuilder::AddSendBuffer(u64 address, u64 size) {
    send_buffers.push_back(Buffer{address, size});
    return *this;
}

RequestBuilder& RequestBuilder::AddReceiveBuffer(u64 address, u64 size) {
    receive_buffers.push_back(Buffer{address, size});
    return *this;
}

std::array<u32_le, IPC::COMMAND_BUFFER_LENGTH> RequestBuilder::Build(u32 object_id) const {
    std::array<u32_le, IPC::COMMAND_BUFFER_LENGTH> buffer{};
    std::size_t index = 0;

    const auto append = [&buffer, &index](const auto& value) {
        const std::size_t words = WordsFor(sizeof(value));
        if (index + words > buffer.size()) {
            throw std::runtime_error{"the request does not fit in a command buffer"};
        }
        std::memcpy(buffer.data() + index, std::addressof(value), sizeof(value));
        index += words;
    };

    // The header's data_size is only known once everything after it is placed.
    const std::size_t header_index = index;
    index += WordsFor(sizeof(IPC::CommandHeader));

    for (const auto& send : send_buffers) {
        append(MakeDescriptor(send.address, send.size));
    }
    for (const auto& receive : receive_buffers) {
        append(MakeDescriptor(receive.address, receive.size));
    }

    // data_size counts from here, before the padding, which is where ParseCommandBuffer starts
    // measuring it to find the C descriptors.
    const std::size_t data_start = index;
    index += (index & 3) != 0 ? 4 - (index & 3) : 0;

    const std::size_t payload_bytes =
        (WordsFor(sizeof(IPC::DataPayloadHeader)) + 2 + WordsFor(raw.size())) * sizeof(u32);

    if (object_id != 0) {
        IPC::DomainMessageHeader domain{};
        domain.command.Assign(IPC::DomainMessageHeader::CommandType::SendMessage);
        domain.input_object_count.Assign(0);
        domain.size.Assign(static_cast<u32>(payload_bytes));
        domain.object_id = object_id;
        append(domain);
    }

    IPC::DataPayloadHeader payload{};
    payload.magic = Common::MakeMagic('S', 'F', 'C', 'I');
    append(payload);

    // The command id is a u64 on the wire; only the low half is ever dispatched on.
    append(static_cast<u64>(command_id));

    if (index + WordsFor(raw.size()) > buffer.size()) {
        throw std::runtime_error{"the request does not fit in a command buffer"};
    }
    std::memcpy(buffer.data() + index, raw.data(), raw.size());
    index += WordsFor(raw.size());

    IPC::CommandHeader header{};
    header.type.Assign(IPC::CommandType::Request);
    header.num_buf_a_descriptors.Assign(static_cast<u32>(send_buffers.size()));
    header.num_buf_b_descriptors.Assign(static_cast<u32>(receive_buffers.size()));
    header.data_size.Assign(static_cast<u32>(index - data_start));
    std::memcpy(buffer.data() + header_index, std::addressof(header), sizeof(header));

    return buffer;
}

Reply::Reply(const u32* words_, u32 payload_offset_, bool is_domain_)
    : payload_offset{payload_offset_}, index{payload_offset_ + 2}, is_domain{is_domain_} {
    std::copy_n(words_, words.size(), words.begin());
}

Result Reply::GetResult() const {
    // IPC::ResponseBuilder pushes the result first and as a u64, of which only the low half is the
    // code; everything a handler pushes after it starts two words later.
    return Result{words[payload_offset]};
}

u32 Reply::DomainObjectCount() const {
    if (!is_domain) {
        return 0;
    }

    // IPC::ResponseBuilder writes the domain header, then the payload header, then sets the
    // payload offset, so the count sits a fixed six words back.
    return words[payload_offset - WordsFor(sizeof(IPC::DomainMessageHeader)) -
                 WordsFor(sizeof(IPC::DataPayloadHeader))];
}

u32 Reply::DomainObject(u32 object_index) const {
    const u32 count = DomainObjectCount();
    if (object_index >= count) {
        throw std::runtime_error{"the reply carries no such domain object"};
    }

    // The ids go at the end of the raw data section, which is where HLERequestContext puts them
    // from the domain offset ResponseBuilder derived: payload offset plus the declared data size.
    IPC::CommandHeader header{};
    std::memcpy(std::addressof(header), words.data(), sizeof(header));
    const u32 domain_offset = payload_offset + header.data_size.Value() / sizeof(u32);

    return words[domain_offset - count + object_index];
}

u32 Reply::CopyHandle(u32 handle_index) const {
    IPC::CommandHeader header{};
    std::memcpy(std::addressof(header), words.data(), sizeof(header));
    if (!header.enable_handle_descriptor) {
        throw std::runtime_error{"the reply carries no handles"};
    }
    IPC::HandleDescriptorHeader handles{};
    std::memcpy(std::addressof(handles), words.data() + 2, sizeof(handles));
    if (handle_index >= handles.num_handles_to_copy) {
        throw std::runtime_error{"the reply copies no such handle"};
    }
    // After the two header words and the handle descriptor, then the pid when one is sent.
    const u32 first = 3 + (handles.send_current_pid ? 2 : 0);
    return words[first + handle_index];
}

} // namespace TestKernel
