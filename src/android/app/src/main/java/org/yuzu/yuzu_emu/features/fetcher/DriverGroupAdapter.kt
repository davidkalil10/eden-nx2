// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.view.isVisible
import androidx.fragment.app.FragmentActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.ItemDriverGroupBinding
import org.yuzu.yuzu_emu.fragments.DriverFetcherFragment.DriverGroup
import org.yuzu.yuzu_emu.model.DriverViewModel

class DriverGroupAdapter(
    private val activity: FragmentActivity,
    private val driverViewModel: DriverViewModel,
) : RecyclerView.Adapter<DriverGroupAdapter.DriverGroupViewHolder>() {

    companion object {
        const val LARGE_CATALOG_THRESHOLD = 8
        const val SCROLLABLE_RELEASE_THRESHOLD = 4
        const val INITIAL_VISIBLE_RELEASES = 10
    }

    private var driverGroups: List<DriverGroup> = emptyList()
    private val expandedKeys = mutableSetOf<String>()

    fun updateDriverGroups(newDriverGroups: List<DriverGroup>) {
        val keys = newDriverGroups.map { it.name.lowercase() }.toSet()
        expandedKeys.retainAll(keys)
        driverGroups = newDriverGroups
        notifyDataSetChanged()
    }

    inner class DriverGroupViewHolder(
        private val binding: ItemDriverGroupBinding,
    ) : RecyclerView.ViewHolder(binding.root) {

        fun bind(group: DriverGroup) {
            val key = group.name.lowercase()
            binding.textGroupName.text = group.name
            binding.progressGroupReleases.isVisible = false

            if (binding.recyclerReleases.layoutManager == null) {
                val layoutManager = LinearLayoutManager(activity)
                layoutManager.initialPrefetchItemCount = 4
                binding.recyclerReleases.layoutManager = layoutManager
                binding.recyclerReleases.itemAnimator = null
                binding.recyclerReleases.addItemDecoration(
                    SpacingItemDecoration(
                        (activity.resources.displayMetrics.density * 6).toInt(),
                    ),
                )
            }

            val releaseCount = group.releases.size
            val scrollableCatalog = releaseCount >= SCROLLABLE_RELEASE_THRESHOLD
            binding.recyclerReleases.isNestedScrollingEnabled = scrollableCatalog

            val expanded = expandedKeys.contains(key)
            binding.recyclerReleases.isVisible = expanded
            binding.imageDropdownArrow.rotation = if (expanded) 180f else 0f

            val largeCatalog = releaseCount >= LARGE_CATALOG_THRESHOLD

            fun attachReleaseAdapter(group: DriverGroup, staged: Boolean) {
                val limit = if (staged) {
                    INITIAL_VISIBLE_RELEASES
                } else {
                    null
                }
                val adapter = ReleaseAdapter(
                    group.releases,
                    activity,
                    driverViewModel,
                    limit,
                )
                binding.recyclerReleases.adapter = adapter
                if (staged) {
                    binding.recyclerReleases.post { adapter.revealFullCatalog() }
                }
            }

            if (expanded && group.releases.isNotEmpty()) {
                val existing = binding.recyclerReleases.adapter as? ReleaseAdapter
                if (existing == null || existing.releaseCount != group.releases.size) {
                    attachReleaseAdapter(group, staged = largeCatalog)
                }
            } else if (!expanded) {
                binding.recyclerReleases.adapter = null
            }

            fun toggleExpanded() {
                if (bindingAdapterPosition == RecyclerView.NO_POSITION) {
                    return
                }
                val willExpand = !expandedKeys.contains(key)
                if (willExpand) {
                    expandedKeys.add(key)
                } else {
                    expandedKeys.remove(key)
                }

                if (willExpand) {
                    if (largeCatalog && group.releases.isNotEmpty()) {
                        FetcherFoldAnimator.pinReleaseListToMaxHeight(
                            binding.recyclerReleases,
                            binding.root.resources.getDimensionPixelSize(
                                R.dimen.driver_group_releases_max_height,
                            ),
                        )
                        binding.recyclerReleases.adapter = null
                        binding.progressGroupReleases.isVisible = true
                        FetcherFoldAnimator.beginFold(
                            binding.root,
                            binding.imageDropdownArrow,
                            onEnd = afterExpand@{
                                if (!binding.root.isAttachedToWindow) {
                                    return@afterExpand
                                }
                                binding.progressGroupReleases.isVisible = false
                                attachReleaseAdapter(group, staged = true)
                                FetcherFoldAnimator.resetReleaseListHeight(binding.recyclerReleases)
                            },
                        )
                    } else {
                        if (group.releases.isNotEmpty() && binding.recyclerReleases.adapter == null) {
                            attachReleaseAdapter(group, staged = false)
                        }
                        FetcherFoldAnimator.beginFold(
                            binding.root,
                            binding.imageDropdownArrow,
                        )
                    }
                    binding.recyclerReleases.isVisible = true
                } else {
                    FetcherFoldAnimator.beginGroupCollapse(
                        anchor = binding.root,
                        releasesList = binding.recyclerReleases,
                        releaseCount = releaseCount,
                        binding.imageDropdownArrow,
                        onEnd = { binding.recyclerReleases.adapter = null },
                    )
                    binding.recyclerReleases.isVisible = false
                }

                FetcherFoldAnimator.animateChevron(binding.imageDropdownArrow, willExpand)
            }

            binding.root.setOnClickListener { toggleExpanded() }
        }
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DriverGroupViewHolder {
        val binding = ItemDriverGroupBinding.inflate(
            LayoutInflater.from(parent.context),
            parent,
            false,
        )
        return DriverGroupViewHolder(binding)
    }

    override fun onBindViewHolder(holder: DriverGroupViewHolder, position: Int) {
        holder.bind(driverGroups[position])
    }

    override fun getItemCount(): Int = driverGroups.size
}