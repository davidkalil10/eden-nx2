// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.fragments

import android.app.Dialog
import android.content.DialogInterface
import android.os.Bundle
import androidx.core.os.bundleOf
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.setFragmentResult
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.model.ProfileUtils
import org.yuzu.yuzu_emu.utils.SaveDataUserUtil

class SaveDataUserSelectDialogFragment : DialogFragment() {
    private var selectedPosition = 0
    private lateinit var userIndices: IntArray

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val entries = ProfileUtils.loadRegisteredProfiles().mapNotNull { profile ->
            val index = SaveDataUserUtil.userIndexForUuid(profile.uuid)
            if (index < 0) null else profile.username to index
        }
        if (entries.isEmpty()) {
            return MaterialAlertDialogBuilder(requireContext())
                .setMessage(R.string.profile_no_users_message)
                .setPositiveButton(android.R.string.ok, null)
                .create()
        }

        userIndices = IntArray(entries.size) { entries[it].second }
        selectedPosition = userIndices.indexOf(requireArguments().getInt(DEFAULT_USER_INDEX))
            .coerceAtLeast(0)
        if (savedInstanceState != null) {
            selectedPosition = savedInstanceState.getInt(SELECTED_POSITION, selectedPosition)
        }

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(requireArguments().getInt(TITLE_ID))
            .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                setFragmentResult(
                    REQUEST_KEY,
                    bundleOf(
                        RESULT_USER_INDEX to userIndices[selectedPosition],
                        RESULT_ACTION to requireArguments().getString(RESULT_ACTION)
                    )
                )
            }
            .setSingleChoiceItems(
                entries.map { it.first }.toTypedArray(),
                selectedPosition
            ) { _, position -> selectedPosition = position }
            .setNegativeButton(android.R.string.cancel, null)
            .create()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt(SELECTED_POSITION, selectedPosition)
    }

    companion object {
        const val TAG = "SaveDataUserSelectDialogFragment"
        const val REQUEST_KEY = "save_data_user_select"

        const val RESULT_USER_INDEX = "user_index"
        const val RESULT_ACTION = "action"

        const val ACTION_EXPORT = "export"
        const val ACTION_IMPORT = "import"
        const val ACTION_DELETE = "delete"

        private const val TITLE_ID = "title_id"
        private const val DEFAULT_USER_INDEX = "default_user_index"
        private const val SELECTED_POSITION = "selected_position"

        fun newInstance(titleId: Int, defaultUserIndex: Int, action: String) =
            SaveDataUserSelectDialogFragment().apply {
                arguments = bundleOf(
                    TITLE_ID to titleId,
                    DEFAULT_USER_INDEX to defaultUserIndex,
                    RESULT_ACTION to action
                )
            }
    }
}