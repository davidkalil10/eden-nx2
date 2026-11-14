// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.adapters

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.view.isGone
import androidx.core.view.isVisible
import androidx.recyclerview.widget.AsyncListDiffer
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.RecyclerView
import org.yuzu.yuzu_emu.databinding.ListItemProfileBinding
import org.yuzu.yuzu_emu.model.ProfileUtils
import org.yuzu.yuzu_emu.model.UserProfile

class PerGameProfileSelectAdapter(
    private val onProfileClick: (UserProfile) -> Unit
) : RecyclerView.Adapter<PerGameProfileSelectAdapter.ProfileViewHolder>() {

    private var selectedUuid: String? = null

    private val differ = AsyncListDiffer(this, object : DiffUtil.ItemCallback<UserProfile>() {
        override fun areItemsTheSame(oldItem: UserProfile, newItem: UserProfile) =
            oldItem.uuid == newItem.uuid

        override fun areContentsTheSame(oldItem: UserProfile, newItem: UserProfile) =
            oldItem == newItem
    })

    fun submitList(list: List<UserProfile>) {
        differ.submitList(list)
    }

    fun setSelectedUuid(uuid: String?) {
        if (selectedUuid == uuid) {
            return
        }
        selectedUuid = uuid
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ProfileViewHolder {
        val binding = ListItemProfileBinding.inflate(LayoutInflater.from(parent.context), parent, false)
        binding.buttonEdit.isGone = true
        binding.buttonDelete.isGone = true
        return ProfileViewHolder(binding)
    }

    override fun onBindViewHolder(holder: ProfileViewHolder, position: Int) {
        holder.bind(differ.currentList[position], selectedUuid)
    }

    override fun getItemCount(): Int = differ.currentList.size

    inner class ProfileViewHolder(
        private val binding: ListItemProfileBinding
    ) : RecyclerView.ViewHolder(binding.root) {

        fun bind(profile: UserProfile, selectedUuid: String?) {
            binding.textUsername.text = profile.username
            binding.textUuid.text = ProfileUtils.formatDisplayUuid(profile.uuid)

            ProfileUtils.bindProfileAvatar(binding.imageAvatar, profile.imagePath)

            binding.checkContainer.isVisible = profile.uuid == selectedUuid
            binding.root.setOnClickListener { onProfileClick(profile) }
        }
    }
}
