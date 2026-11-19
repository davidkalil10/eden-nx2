// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.view.isVisible
import androidx.recyclerview.widget.RecyclerView
import org.yuzu.yuzu_emu.databinding.ItemCustomDriverLinkBinding
import org.yuzu.yuzu_emu.utils.GpuDriverHelper

class CustomDriverLinkAdapter(
    private val onDownload: (CustomDriverLink) -> Unit,
    private val onEdit: (CustomDriverLink) -> Unit,
    private val onDelete: (CustomDriverLink) -> Unit,
) : RecyclerView.Adapter<CustomDriverLinkAdapter.LinkViewHolder>() {

    private var links: List<CustomDriverLink> = emptyList()

    inner class LinkViewHolder(
        private val binding: ItemCustomDriverLinkBinding,
    ) : RecyclerView.ViewHolder(binding.root) {

        fun bind(link: CustomDriverLink) {
            binding.title.text = link.label
            binding.url.text = link.url

            when (val state = link.cardState()) {
                CustomDriverLinkCardState.INVALID -> {
                    binding.textUrlError.isVisible = true
                    binding.buttonDownload.isVisible = false
                }

                CustomDriverLinkCardState.CATALOG -> {
                    binding.textUrlError.isVisible = false
                    binding.buttonDownload.isVisible = false
                }

                CustomDriverLinkCardState.DIRECT_DOWNLOAD -> {
                    binding.textUrlError.isVisible = false
                    binding.buttonDownload.isVisible = true
                }
            }

            if (binding.buttonDownload.isVisible) {
                val fileName = CustomDriverLinkStore.fileNameForLink(link)
                val installed = GpuDriverHelper.isDriverZipInstalledByName(fileName)
                binding.buttonDownload.isEnabled = !installed
                binding.buttonDownload.text = if (installed) {
                    binding.root.context.getString(
                        org.yuzu.yuzu_emu.R.string.installed_label,
                        fileName,
                    )
                } else {
                    binding.root.context.getString(org.yuzu.yuzu_emu.R.string.download)
                }
                binding.buttonDownload.setOnClickListener { onDownload(link) }
            }

            binding.buttonEdit.setOnClickListener { onEdit(link) }
            binding.buttonDelete.setOnClickListener { onDelete(link) }
        }
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): LinkViewHolder {
        val binding = ItemCustomDriverLinkBinding.inflate(
            LayoutInflater.from(parent.context),
            parent,
            false
        )
        return LinkViewHolder(binding)
    }

    override fun onBindViewHolder(holder: LinkViewHolder, position: Int) {
        holder.bind(links[position])
    }

    override fun getItemCount(): Int = links.size

    fun submitList(newLinks: List<CustomDriverLink>) {
        links = newLinks
        notifyDataSetChanged()
    }
}