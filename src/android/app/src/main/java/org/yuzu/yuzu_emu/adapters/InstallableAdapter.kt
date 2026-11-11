// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.adapters

import android.view.LayoutInflater
import android.view.ViewGroup
import org.yuzu.yuzu_emu.databinding.CardInstallableBinding
import org.yuzu.yuzu_emu.databinding.CardInstallableIconBinding
import org.yuzu.yuzu_emu.model.Installable
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible
import org.yuzu.yuzu_emu.viewholder.AbstractViewHolder

class InstallableAdapter(installables: List<Installable>) :
    AbstractListAdapter<Installable, AbstractViewHolder<Installable>>(installables) {

    override fun getItemViewType(position: Int): Int =
        if (currentList[position].iconId != 0) VIEW_TYPE_ICON else VIEW_TYPE_DEFAULT

    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): AbstractViewHolder<Installable> {
        val inflater = LayoutInflater.from(parent.context)
        return when (viewType) {
            VIEW_TYPE_ICON -> {
                InstallableIconViewHolder(
                    CardInstallableIconBinding.inflate(inflater, parent, false)
                )
            }
            else -> {
                InstallableViewHolder(CardInstallableBinding.inflate(inflater, parent, false))
            }
        }
    }

    inner class InstallableViewHolder(val binding: CardInstallableBinding) :
        AbstractViewHolder<Installable>(binding) {
        override fun bind(model: Installable) {
            binding.title.setText(model.titleId)
            binding.description.setText(model.descriptionId)

            binding.buttonInstall.setVisible(model.install != null)
            binding.buttonInstall.setOnClickListener { model.install?.invoke() }
            binding.buttonExport.setVisible(model.export != null)
            binding.buttonExport.setOnClickListener { model.export?.invoke() }
        }
    }

    inner class InstallableIconViewHolder(val binding: CardInstallableIconBinding) :
        AbstractViewHolder<Installable>(binding) {
        override fun bind(model: Installable) {
            binding.title.setText(model.titleId)
            binding.description.setText(model.descriptionId)
            binding.icon.setImageResource(model.iconId)

            binding.buttonInstall.setVisible(model.install != null)
            binding.buttonInstall.setOnClickListener { model.install?.invoke() }
            binding.buttonExport.setVisible(model.export != null)
            binding.buttonExport.setOnClickListener { model.export?.invoke() }
        }
    }

    private companion object {
        const val VIEW_TYPE_DEFAULT = 0
        const val VIEW_TYPE_ICON = 1
    }
}
