// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstring>
#include <type_traits>
#include <vector>

#include <boost/container/static_vector.hpp>

#include "common/common_types.h"
#include "core/hle/ipc.h"
#include "core/hle/result.h"

namespace TestKernel {

/// Words a raw parameter of @p bytes takes up, the rounding IPC::RequestParser::PopRaw applies.
[[nodiscard]] constexpr std::size_t WordsFor(std::size_t bytes) {
    return (bytes + sizeof(u32) - 1) / sizeof(u32);
}

/**
 * Lays out the command buffer a guest would have written for one HIPC request.
 *
 * IPC::CommandHeader and the descriptor structs can be read but not written: the emulator only
 * ever parses a request, because a real guest builds one. This is the missing half, and it
 * mirrors HLERequestContext::ParseCommandBuffer field for field, including the 16-byte alignment
 * ahead of the payload header. Get that wrong and IPC::RequestParser's "context is incomplete"
 * assert fires inside the service, which reads like a service bug.
 *
 * Only what a command-level test needs: no X or W descriptors, no handles, no C descriptors.
 */
class RequestBuilder {
public:
    explicit RequestBuilder(u32 command_id_) : command_id{command_id_} {}

    /// Appends one raw parameter, rounded to a word the way the handler will pop it.
    template <typename T>
    RequestBuilder& PushRaw(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "raw parameters are memcpy'd");
        const std::size_t offset = raw.size();
        raw.resize(offset + WordsFor(sizeof(T)) * sizeof(u32));
        std::memcpy(raw.data() + offset, std::addressof(value), sizeof(T));
        return *this;
    }

    /// A buffer the handler reads: descriptor A, the HipcMapAlias send side.
    RequestBuilder& AddSendBuffer(u64 address, u64 size);

    /// A buffer the handler writes: descriptor B, the HipcMapAlias receive side.
    RequestBuilder& AddReceiveBuffer(u64 address, u64 size);

    /**
     * The finished command buffer.
     *
     * @param object_id the domain object to address, 1-based as the guest counts them, or 0 for a
     *                  session that is not a domain. A domain request carries an extra header, and
     *                  HLERequestContext looks for one only when the session's manager is a
     *                  domain, so the session decides this, not the builder.
     */
    [[nodiscard]] std::array<u32_le, IPC::COMMAND_BUFFER_LENGTH> Build(u32 object_id) const;

private:
    struct Buffer {
        u64 address;
        u64 size;
    };

    u32 command_id;
    std::vector<u8> raw;
    boost::container::static_vector<Buffer, IPC::MAX_BUFFER_DESCRIPTORS> send_buffers;
    boost::container::static_vector<Buffer, IPC::MAX_BUFFER_DESCRIPTORS> receive_buffers;
};

/**
 * The reply a handler left behind, read the way a guest reads one.
 *
 * Out of the context's own command buffer rather than out of guest memory, because the copy into
 * the caller's thread-local region is the one step of the reply path a test cannot reproduce: a
 * dummy thread has no thread-local region and the kernel offers no way to give it one.
 */
class Reply {
public:
    Reply(const u32* words_, u32 payload_offset_, bool is_domain_);

    [[nodiscard]] Result GetResult() const;

    /// The next raw parameter, with the word rounding the handler pushed it under.
    template <typename T>
    [[nodiscard]] T Pop() {
        static_assert(std::is_trivially_copyable_v<T>, "raw parameters are memcpy'd");
        T value{};
        std::memcpy(std::addressof(value), words.data() + index, sizeof(T));
        index += static_cast<u32>(WordsFor(sizeof(T)));
        return value;
    }

    /// How many interfaces the reply hands back. Only a domain reply can carry any.
    [[nodiscard]] u32 DomainObjectCount() const;

    /// One of their ids, as the guest would use it to address that interface.
    [[nodiscard]] u32 DomainObject(u32 object_index) const;

    /// A handle the reply copied into the calling process's table, in the order it was pushed.
    [[nodiscard]] u32 CopyHandle(u32 handle_index) const;

private:
    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> words{};
    u32 payload_offset{};
    u32 index{};
    bool is_domain{};
};

} // namespace TestKernel
