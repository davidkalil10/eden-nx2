// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isVisible
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.fragment.navArgs
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.transition.MaterialSharedAxis
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.adapters.PerGameProfileSelectAdapter
import org.yuzu.yuzu_emu.databinding.FragmentPerGameUserProfileBinding
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.features.settings.utils.SettingsFile
import org.yuzu.yuzu_emu.model.HomeViewModel
import org.yuzu.yuzu_emu.model.ProfileUtils
import org.yuzu.yuzu_emu.model.UserProfile
import org.yuzu.yuzu_emu.utils.NativeConfig
import org.yuzu.yuzu_emu.utils.PerGameUserProfileUtil
import org.yuzu.yuzu_emu.utils.ViewUtils.updateMargins

class PerGameUserProfileFragment : Fragment() {
    private var _binding: FragmentPerGameUserProfileBinding? = null
    private val binding get() = _binding!!

    private val homeViewModel: HomeViewModel by activityViewModels()
    private val args by navArgs<PerGameUserProfileFragmentArgs>()

    private lateinit var profileAdapter: PerGameProfileSelectAdapter
    private var perGameConfigLoaded = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        exitTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentPerGameUserProfileBinding.inflate(layoutInflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setStatusBarShadeVisibility(visible = false)

        if (!NativeConfig.isPerGameConfigLoaded()) {
            SettingsFile.loadCustomConfig(args.game)
            perGameConfigLoaded = true
        }

        binding.toolbar.setNavigationOnClickListener {
            requireActivity().onBackPressedDispatcher.onBackPressed()
        }

        profileAdapter = PerGameProfileSelectAdapter(::selectProfile)
        binding.listProfiles.apply {
            layoutManager = LinearLayoutManager(requireContext())
            adapter = profileAdapter
        }

        binding.globalProfileOption.cardGlobalProfile.setOnClickListener {
            selectGlobalProfile()
        }

        refreshSelectionUi()
        setInsets()
    }

    override fun onResume() {
        super.onResume()
        refreshSelectionUi()
    }

    override fun onDestroy() {
        if (perGameConfigLoaded && NativeConfig.isPerGameConfigLoaded()) {
            NativeConfig.unloadPerGameConfig()
            NativeConfig.reloadGlobalConfig()
        }
        super.onDestroy()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun refreshSelectionUi() {
        profileAdapter.submitList(ProfileUtils.loadRegisteredProfiles())

        val useGlobal = IntSetting.CURRENT_USER.global
        binding.globalProfileOption.checkContainer.isVisible = useGlobal

        val selectedUuid = if (useGlobal) {
            null
        } else {
            val index = IntSetting.CURRENT_USER.getInt(false)
            NativeLibrary.getAllUsers()?.getOrNull(index)?.takeIf { it.isNotEmpty() }
        }
        profileAdapter.setSelectedUuid(selectedUuid)
    }

    private fun persistAndRefresh() {
        NativeConfig.savePerGameConfig()
        homeViewModel.reloadPropertiesList(true)
        refreshSelectionUi()
    }

    private fun selectGlobalProfile() {
        IntSetting.CURRENT_USER.global = true
        persistAndRefresh()
    }

    private fun selectProfile(profile: UserProfile) {
        val index = PerGameUserProfileUtil.userIndexForUuid(profile.uuid)
        if (index < 0) {
            return
        }
        IntSetting.CURRENT_USER.global = false
        IntSetting.CURRENT_USER.setInt(index)
        persistAndRefresh()
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { _, windowInsets ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.appbar.updateMargins(left = leftInsets, right = rightInsets)
            binding.scrollContent.updateMargins(left = leftInsets, right = rightInsets)
            binding.scrollContent.updatePadding(bottom = barInsets.bottom)
            windowInsets
        }
}
