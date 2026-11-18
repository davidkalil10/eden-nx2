// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.ui

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.content.res.ColorStateList
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.PathInterpolator
import android.view.inputmethod.InputMethodManager
import android.widget.PopupMenu
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.doOnPreDraw
import androidx.core.view.updatePadding
import androidx.core.widget.ImageViewCompat
import androidx.core.widget.doOnTextChanged
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.fragment.findNavController
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.RecyclerView
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout
import com.google.android.material.color.MaterialColors
import org.yuzu.yuzu_emu.HomeNavigationDirections
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.adapters.GameAdapter
import org.yuzu.yuzu_emu.databinding.FragmentGamesBinding
import org.yuzu.yuzu_emu.features.settings.model.BooleanSetting
import org.yuzu.yuzu_emu.model.AppletInfo
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.model.GamesViewModel
import org.yuzu.yuzu_emu.model.HomeViewModel
import org.yuzu.yuzu_emu.ui.main.MainActivity
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible
import org.yuzu.yuzu_emu.utils.collect
import info.debatty.java.stringsimilarity.Jaccard
import info.debatty.java.stringsimilarity.JaroWinkler
import java.util.Locale
import androidx.core.content.edit

class GamesFragment : Fragment() {
    private var _binding: FragmentGamesBinding? = null
    private val binding get() = _binding!!

    private var originalHeaderTopMargin: Int? = null
    private var originalHeaderBottomMargin: Int? = null
    private var originalHeaderRightMargin: Int? = null
    private var originalHeaderLeftMargin: Int? = null

    private var lastViewType: Int = GameAdapter.VIEW_TYPE_GRID
    private var pendingPostReloadListSettle = false
    private var pendingPostReloadListSettleGeneration = 0
    private var gameListSubmitGeneration = 0
    private var committedGameListSubmitGeneration = 0
    private var gamesSearchExpanded = false
    private var portraitSearchLayoutShiftPx = 0f

    companion object {
        private const val SEARCH_TEXT = "SearchText"
        private const val PREF_SORT_TYPE = "GamesSortType"
        private const val PREF_GAMES_SEARCH_EXPANDED = "GamesSearchExpanded"
        private const val PREF_GAMES_SEARCH_QUERY = "GamesSearchQuery"
        private const val SEARCH_GRID_ANIM_DURATION_MS = 380L
        private val SEARCH_GRID_EXPAND_INTERPOLATOR = PathInterpolator(0.05f, 0.7f, 0.1f, 1f)
        private val SEARCH_GRID_COLLAPSE_INTERPOLATOR = PathInterpolator(0.3f, 0f, 1f, 1f)
    }

    private val gamesViewModel: GamesViewModel by activityViewModels()
    private val homeViewModel: HomeViewModel by activityViewModels()
    private lateinit var gameAdapter: GameAdapter

    private val preferences =
        PreferenceManager.getDefaultSharedPreferences(YuzuApplication.appContext)

    private lateinit var mainActivity: MainActivity
    private val getGamesDirectory =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { result ->
            if (result != null) {
                mainActivity.processGamesDir(result, true)
            }
        }

    private fun getCurrentViewType(): Int {
        val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        val key = if (isLandscape) CarouselRecyclerView.CAROUSEL_VIEW_TYPE_LANDSCAPE else CarouselRecyclerView.CAROUSEL_VIEW_TYPE_PORTRAIT
        val fallback = if (isLandscape) GameAdapter.VIEW_TYPE_CAROUSEL else GameAdapter.VIEW_TYPE_GRID
        return preferences.getInt(key, fallback)
    }

    private fun setCurrentViewType(type: Int) {
        val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        val key = if (isLandscape) CarouselRecyclerView.CAROUSEL_VIEW_TYPE_LANDSCAPE else CarouselRecyclerView.CAROUSEL_VIEW_TYPE_PORTRAIT
        preferences.edit { putInt(key, type) }
    }
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentGamesBinding.inflate(inflater)
        return binding.root
    }

    @SuppressLint("NotifyDataSetChanged")
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setStatusBarShadeVisibility(true)
        mainActivity = requireActivity() as MainActivity

        if (savedInstanceState != null) {
            binding.searchText.setText(savedInstanceState.getString(SEARCH_TEXT))
            gamesSearchExpanded = savedInstanceState.getBoolean(PREF_GAMES_SEARCH_EXPANDED, false)
        } else {
            gamesSearchExpanded = preferences.getBoolean(PREF_GAMES_SEARCH_EXPANDED, false)
            binding.searchText.setText(preferences.getString(PREF_GAMES_SEARCH_QUERY, ""))
        }

        gameAdapter = GameAdapter(
            requireActivity() as AppCompatActivity
        )

        applyGridGamesBinding()

        binding.swipeRefresh.apply {
            (binding.swipeRefresh as? SwipeRefreshLayout)?.setOnRefreshListener {
                gamesViewModel.reloadGames(false)
            }
            (binding.swipeRefresh as? SwipeRefreshLayout)?.setProgressBackgroundColorSchemeColor(
                com.google.android.material.color.MaterialColors.getColor(
                    binding.swipeRefresh,
                    com.google.android.material.R.attr.colorPrimary
                )
            )
            (binding.swipeRefresh as? SwipeRefreshLayout)?.setColorSchemeColors(
                com.google.android.material.color.MaterialColors.getColor(
                    binding.swipeRefresh,
                    com.google.android.material.R.attr.colorOnPrimary
                )
            )
            post {
                if (_binding == null) {
                    return@post
                }
                (binding.swipeRefresh as? SwipeRefreshLayout)?.isRefreshing = gamesViewModel.isReloading.value
            }
        }

        gamesViewModel.isReloading.collect(viewLifecycleOwner) {
            (binding.swipeRefresh as? SwipeRefreshLayout)?.isRefreshing = it
            binding.noticeText.setVisible(
                visible = gamesViewModel.games.value.isEmpty() && !it,
                gone = false
            )
        }
        gamesViewModel.games.collect(viewLifecycleOwner) {
            if (_binding != null) {
                setAdapter(it)
            }
        }
        gamesViewModel.shouldSwapData.collect(
            viewLifecycleOwner,
            resetState = { gamesViewModel.setShouldSwapData(false) }
        ) {
            if (it) {
                setAdapter(gamesViewModel.games.value)
            }
        }
        gamesViewModel.shouldScrollToTop.collect(
            viewLifecycleOwner,
            resetState = { gamesViewModel.setShouldScrollToTop(false) }
        ) { if (it) scrollToTop() }

        gamesViewModel.shouldScrollAfterReload.collect(viewLifecycleOwner) { shouldScroll ->
            if (shouldScroll) {
                pendingPostReloadListSettle = true
                pendingPostReloadListSettleGeneration = gameListSubmitGeneration
                schedulePostReloadListSettle()
                gamesViewModel.setShouldScrollAfterReload(false)
            }
        }

        setupTopView()
        setupCollapsibleSearch()

        updateButtonsVisibility()

        binding.addDirectory.setOnClickListener {
            getGamesDirectory.launch(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).data)
        }

        binding.launchQlaunch?.setOnClickListener {
            launchQLaunch()
        }

        setInsets()
    }

    val applyGridGamesBinding = {
        (binding.gridGames as? RecyclerView)?.apply {
            val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
            val currentViewType = getCurrentViewType()
            val savedViewType = if (isLandscape || currentViewType != GameAdapter.VIEW_TYPE_CAROUSEL) currentViewType else GameAdapter.VIEW_TYPE_GRID

            //This prevents Grid/List views from reusing scaled or otherwise modified ViewHolders left over from the carousel.
            adapter = null
            recycledViewPool.clear()

            gameAdapter.setViewType(savedViewType)
            currentFilter = preferences.getInt(PREF_SORT_TYPE, View.NO_ID)

            // Set the correct layout manager
            layoutManager = when (savedViewType) {
                GameAdapter.VIEW_TYPE_GRID -> {
                    val columns = resources.getInteger(R.integer.game_columns_grid)
                    GridLayoutManager(context, columns)
                }
                GameAdapter.VIEW_TYPE_GRID_COMPACT -> {
                    val columns = resources.getInteger(R.integer.game_columns_grid)
                    GridLayoutManager(context, columns)
                }
                GameAdapter.VIEW_TYPE_LIST -> {
                    val columns = resources.getInteger(R.integer.game_columns_list)
                    GridLayoutManager(context, columns)
                }
                GameAdapter.VIEW_TYPE_CAROUSEL -> {
                    LinearLayoutManager(context, RecyclerView.HORIZONTAL, false)
                }
                else -> throw IllegalArgumentException("Invalid view type: $savedViewType")
            }
            if (savedViewType != GameAdapter.VIEW_TYPE_CAROUSEL) {
                (this as? CarouselRecyclerView)?.setupCarousel(false)
            }
            adapter = gameAdapter
            lastViewType = savedViewType
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        if (_binding != null) {
            outState.putString(SEARCH_TEXT, binding.searchText.text.toString())
            outState.putBoolean(PREF_GAMES_SEARCH_EXPANDED, gamesSearchExpanded)
        }
    }

    override fun onPause() {
        super.onPause()
        persistGamesSearchUiState()
        if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) {
            gamesViewModel.lastScrollPosition = (binding.gridGames as? CarouselRecyclerView)?.getClosestChildPosition() ?: 0
        }
    }

    override fun onResume() {
        super.onResume()
        if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) {
            (binding.gridGames as? CarouselRecyclerView)?.setupCarousel(true)
            (binding.gridGames as? CarouselRecyclerView)?.restoreScrollState(gamesViewModel.lastScrollPosition)
        }
    }

    private var lastSearchText: String = ""
    private var lastFilter: Int = preferences.getInt(PREF_SORT_TYPE, View.NO_ID)

    private fun setAdapter(games: List<Game>) {
        val currentSearchText = binding.searchText.text.toString()
        val currentFilter = this.currentFilter

        val searchChanged = currentSearchText != lastSearchText
        val filterChanged = currentFilter != lastFilter

        if (searchChanged || filterChanged) {
            filterAndSearch(games)
            lastSearchText = currentSearchText
            lastFilter = currentFilter
        } else {
            submitGameList(games)
            gamesViewModel.setFilteredGames(games)
        }
    }

    private fun submitGameList(games: List<Game>) {
        val adapter = (binding.gridGames as? RecyclerView)?.adapter as? GameAdapter
        if (adapter == null) {
            schedulePostReloadListSettle()
            return
        }

        val submitGeneration = ++gameListSubmitGeneration
        adapter.submitList(games) {
            if (committedGameListSubmitGeneration < submitGeneration) {
                committedGameListSubmitGeneration = submitGeneration
            }
            schedulePostReloadListSettle()
        }
    }


    private fun schedulePostReloadListSettle() {
        if (!pendingPostReloadListSettle || _binding == null) return

        binding.gridGames.doOnPreDraw {
            if (!pendingPostReloadListSettle || _binding == null) return@doOnPreDraw
            if (committedGameListSubmitGeneration < pendingPostReloadListSettleGeneration) {
                schedulePostReloadListSettle()
                return@doOnPreDraw
            }
            pendingPostReloadListSettle = false

            (binding.gridGames as? CarouselRecyclerView)?.refreshView()
        }
    }
    private fun setupTopView() {
        binding.searchText.doOnTextChanged() { text: CharSequence?, _: Int, _: Int, _: Int ->
            if (text.toString().isNotEmpty()) {
                binding.clearButton.visibility = View.VISIBLE
            } else {
                binding.clearButton.visibility = View.INVISIBLE
            }
            filterAndSearch()
        }

        binding.searchBackground.setOnClickListener { focusSearch() }
        binding.viewSortButton.setOnClickListener { showViewSortMenu(it) }
        binding.settingsButton.setOnClickListener { navigateToSettings() }
    }

    private fun navigateToSettings() {
        val navController = findNavController()
        navController.navigate(R.id.action_gamesFragment_to_homeSettingsFragment)
    }

    private var currentFilter = View.NO_ID

    private fun showViewSortMenu(anchor: View) {
        val popup = PopupMenu(requireContext(), anchor)
        popup.menuInflater.inflate(R.menu.menu_game_view_sort, popup.menu)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            popup.menu.setGroupDividerEnabled(true)
        }

        val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        if (!isLandscape) {
            popup.menu.findItem(R.id.view_carousel)?.isVisible = false
        }

        when (getCurrentViewType()) {
            GameAdapter.VIEW_TYPE_LIST -> popup.menu.findItem(R.id.view_list).isChecked = true
            GameAdapter.VIEW_TYPE_GRID_COMPACT -> popup.menu.findItem(R.id.view_grid_compact).isChecked = true
            GameAdapter.VIEW_TYPE_CAROUSEL -> popup.menu.findItem(R.id.view_carousel).isChecked = true
            else -> popup.menu.findItem(R.id.view_grid).isChecked = true
        }

        when (currentFilter) {
            R.id.filter_recently_played -> popup.menu.findItem(R.id.filter_recently_played).isChecked = true
            R.id.filter_recently_added -> popup.menu.findItem(R.id.filter_recently_added).isChecked = true
            else -> popup.menu.findItem(R.id.alphabetical).isChecked = true
        }

        styleViewSortMenuHeadings(popup.menu, anchor)

        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                R.id.view_grid -> {
                    if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) onPause()
                    setCurrentViewType(GameAdapter.VIEW_TYPE_GRID)
                    applyGridGamesBinding()
                    item.isChecked = true
                    true
                }

                R.id.view_grid_compact -> {
                    if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) onPause()
                    setCurrentViewType(GameAdapter.VIEW_TYPE_GRID_COMPACT)
                    applyGridGamesBinding()
                    item.isChecked = true
                    true
                }

                R.id.view_list -> {
                    if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) onPause()
                    setCurrentViewType(GameAdapter.VIEW_TYPE_LIST)
                    applyGridGamesBinding()
                    item.isChecked = true
                    true
                }

                R.id.view_carousel -> {
                    if (!item.isChecked || getCurrentViewType() != GameAdapter.VIEW_TYPE_CAROUSEL) {
                        setCurrentViewType(GameAdapter.VIEW_TYPE_CAROUSEL)
                        applyGridGamesBinding()
                        item.isChecked = true
                        onResume()
                    }
                    true
                }

                R.id.alphabetical,
                R.id.filter_recently_played,
                R.id.filter_recently_added -> {
                    currentFilter = item.itemId
                    preferences.edit { putInt(PREF_SORT_TYPE, currentFilter) }
                    filterAndSearch()
                    true
                }

                else -> false
            }
        }

        popup.show()
    }

    private fun styleViewSortMenuHeadings(menu: android.view.Menu, anchor: View) {
        val titleColor = MaterialColors.getColor(
            anchor,
            com.google.android.material.R.attr.colorOnSurface
        )
        listOf(R.id.menu_heading_view, R.id.menu_heading_sort).forEach { id ->
            menu.findItem(id)?.let { item ->
                item.isEnabled = false
                item.isCheckable = false
                val label = item.title?.toString().orEmpty()
                if (label.isNotEmpty()) {
                    item.title = SpannableString(label).apply {
                        setSpan(
                            ForegroundColorSpan(titleColor),
                            0,
                            length,
                            Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                        )
                    }
                }
            }
        }
    }

    private fun filterAndSearch(baseList: List<Game> = gamesViewModel.games.value) {
        val filteredList: List<Game> = when (currentFilter) {
            R.id.alphabetical -> baseList.sortedBy { it.title }
            R.id.filter_recently_played -> {
                baseList.filter {
                    val lastPlayedTime = preferences.getLong(it.keyLastPlayedTime, 0L)
                    lastPlayedTime > (System.currentTimeMillis() - 24 * 60 * 60 * 1000)
                }.sortedByDescending { preferences.getLong(it.keyLastPlayedTime, 0L) }
            }
            R.id.filter_recently_added -> {
                baseList.filter {
                    val addedTime = preferences.getLong(it.keyAddedToLibraryTime, 0L)
                    addedTime > (System.currentTimeMillis() - 24 * 60 * 60 * 1000)
                }.sortedByDescending { preferences.getLong(it.keyAddedToLibraryTime, 0L) }
            }
            else -> baseList
        }

        val searchTerm = binding.searchText.text.toString().lowercase(Locale.getDefault())
        if (searchTerm.isEmpty()) {
            submitGameList(filteredList)
            gamesViewModel.setFilteredGames(filteredList)
            return
        }

        val searchAlgorithm = if (searchTerm.length > 1) Jaccard(2) else JaroWinkler()
        val sortedList = filteredList.mapNotNull { game ->
            val title = game.title.lowercase(Locale.getDefault())
            val score = searchAlgorithm.similarity(searchTerm, title)
            if (score > 0.03) {
                ScoredGame(score, game)
            } else {
                null
            }
        }.sortedByDescending { it.score }.map { it.item }

        submitGameList(sortedList)
        gamesViewModel.setFilteredGames(sortedList)
    }

    private inner class ScoredGame(val score: Double, val item: Game)

    private fun setupCollapsibleSearch() {
        pinHeaderAboveSearchBar()
        binding.frameSearch.visibility = View.GONE
        binding.searchButton.setOnClickListener {
            setGamesSearchExpanded(!gamesSearchExpanded, animated = true)
        }
        binding.clearButton.setOnClickListener { binding.searchText.setText("") }
        setGamesSearchExpanded(
            gamesSearchExpanded || binding.searchText.text.isNotEmpty(),
            animated = false,
        )
    }

    private fun persistGamesSearchUiState() {
        preferences.edit {
            putBoolean(PREF_GAMES_SEARCH_EXPANDED, gamesSearchExpanded)
            putString(PREF_GAMES_SEARCH_QUERY, binding.searchText.text.toString())
        }
    }

    private fun updateSearchToggleIcon() {
        val showClose = gamesSearchExpanded
        binding.searchButtonIcon.setImageResource(
            if (showClose) R.drawable.ic_search_close else R.drawable.ic_search
        )
        val accent = MaterialColors.getColor(
            binding.searchButtonIcon,
            com.google.android.material.R.attr.colorSecondary
        )
        ImageViewCompat.setImageTintList(
            binding.searchButtonIcon,
            ColorStateList.valueOf(accent)
        )
        binding.searchButton.contentDescription = getString(
            if (showClose) R.string.cancel else R.string.home_search_games
        )
    }

    private fun updateInlineSearchHeaderSlot() {
        if (resources.configuration.orientation != Configuration.ORIENTATION_LANDSCAPE) {
            return
        }
        val spacer = binding.root.findViewById<View>(R.id.header_flex_spacer) ?: return
        spacer.visibility = if (gamesSearchExpanded) View.GONE else View.VISIBLE
        binding.frameSearch.visibility = if (gamesSearchExpanded) View.VISIBLE else View.GONE
    }

    private fun resetPortraitSearchMotionOffsets() {
        binding.swipeRefresh.animate().cancel()
        binding.frameSearch.animate().cancel()
        binding.swipeRefresh.translationY = 0f
        binding.frameSearch.translationY = 0f
        binding.frameSearch.alpha = 1f
    }

    private fun portraitSearchSlideShiftPx(): Float {
        if (portraitSearchLayoutShiftPx > 0f) {
            return portraitSearchLayoutShiftPx
        }
        val bar = binding.frameSearch
        val marginTop =
            (bar.layoutParams as? ViewGroup.MarginLayoutParams)?.topMargin ?: 0
        return (bar.height + marginTop).toFloat().coerceAtLeast(1f)
    }

    private fun pinHeaderAboveSearchBar() {
        if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            return
        }
        val headerElevation = 4f * resources.displayMetrics.density
        binding.header.elevation = headerElevation
        binding.frameSearch.elevation = 0f
        binding.header.bringToFront()
    }

    private fun runPortraitSearchCollapseAnimation(slideShiftPx: Float, onEnd: () -> Unit) {
        val list = binding.swipeRefresh
        val bar = binding.frameSearch
        pinHeaderAboveSearchBar()
        list.animate().cancel()
        bar.animate().cancel()
        list.translationY = 0f
        if (slideShiftPx > 0f) {
            portraitSearchLayoutShiftPx = slideShiftPx
        }
        if (slideShiftPx <= 0f) {
            bar.translationY = 0f
            bar.alpha = 1f
            onEnd()
            return
        }
        val offset = -slideShiftPx
        bar.translationY = 0f
        bar.alpha = 1f
        var finished = 0
        val onPartFinished = {
            finished++
            if (finished >= 2) {
                onEnd()
            }
        }
        list.animate()
            .translationY(offset)
            .setDuration(SEARCH_GRID_ANIM_DURATION_MS)
            .setInterpolator(SEARCH_GRID_COLLAPSE_INTERPOLATOR)
            .withEndAction(onPartFinished)
            .start()
        bar.animate()
            .alpha(0f)
            .setDuration(SEARCH_GRID_ANIM_DURATION_MS)
            .setInterpolator(SEARCH_GRID_COLLAPSE_INTERPOLATOR)
            .withEndAction(onPartFinished)
            .start()
    }

    private fun runPortraitSearchGridAnimation(
        expanding: Boolean,
        slideShiftPx: Float,
        onEnd: () -> Unit,
    ) {
        if (!expanding) {
            runPortraitSearchCollapseAnimation(slideShiftPx, onEnd)
            return
        }
        val list = binding.swipeRefresh
        val bar = binding.frameSearch
        list.animate().cancel()
        bar.animate().cancel()
        if (slideShiftPx <= 0f) {
            list.translationY = 0f
            bar.translationY = 0f
            bar.alpha = 1f
            onEnd()
            return
        }
        portraitSearchLayoutShiftPx = slideShiftPx
        val offset = -slideShiftPx
        list.translationY = offset
        bar.translationY = 0f
        bar.alpha = 0f
        var finished = 0
        val onPartFinished = {
            finished++
            if (finished >= 2) {
                onEnd()
            }
        }
        list.animate()
            .translationY(0f)
            .setDuration(SEARCH_GRID_ANIM_DURATION_MS)
            .setInterpolator(SEARCH_GRID_EXPAND_INTERPOLATOR)
            .withEndAction(onPartFinished)
            .start()
        bar.animate()
            .alpha(1f)
            .setDuration(SEARCH_GRID_ANIM_DURATION_MS)
            .setInterpolator(SEARCH_GRID_EXPAND_INTERPOLATOR)
            .withEndAction(onPartFinished)
            .start()
    }

    private fun beginPortraitSearchExpandAnimation() {
        val listTopBefore = binding.swipeRefresh.top
        binding.frameSearch.visibility = View.INVISIBLE
        binding.root.doOnPreDraw {
            if (_binding == null || !gamesSearchExpanded) {
                return@doOnPreDraw
            }
            val slideShiftPx =
                (binding.swipeRefresh.top - listTopBefore).toFloat().coerceAtLeast(0f)
            binding.frameSearch.visibility = View.VISIBLE
            binding.frameSearch.translationY = 0f
            runPortraitSearchGridAnimation(
                expanding = true,
                slideShiftPx = slideShiftPx,
                onEnd = {},
            )
            binding.searchText.clearFocus()
        }
    }

    private fun setGamesSearchExpanded(expanded: Boolean, animated: Boolean) {
        if (_binding == null) {
            return
        }
        if (expanded && gamesSearchExpanded && binding.frameSearch.visibility == View.VISIBLE) {
            return
        }
        if (!expanded && !gamesSearchExpanded) {
            updateSearchToggleIcon()
            updateInlineSearchHeaderSlot()
            return
        }

        if (!expanded) {
            val imm = requireActivity()
                .getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager?
            imm?.hideSoftInputFromWindow(binding.searchText.windowToken, 0)

            val applyCollapsed = {
                if (_binding != null) {
                    gamesSearchExpanded = false
                    binding.frameSearch.visibility = View.GONE
                    resetPortraitSearchMotionOffsets()
                    binding.searchText.setText("")
                    filterAndSearch()
                    updateSearchToggleIcon()
                    updateInlineSearchHeaderSlot()
                    persistGamesSearchUiState()
                }
            }

            if (!animated || resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
                applyCollapsed()
                return
            }

            if (_binding != null && gamesSearchExpanded) {
                runPortraitSearchGridAnimation(
                    expanding = false,
                    slideShiftPx = portraitSearchSlideShiftPx(),
                    onEnd = applyCollapsed,
                )
            }
            return
        }

        gamesSearchExpanded = true
        updateSearchToggleIcon()
        updateInlineSearchHeaderSlot()
        persistGamesSearchUiState()

        if (resources.configuration.orientation != Configuration.ORIENTATION_LANDSCAPE) {
            if (animated) {
                beginPortraitSearchExpandAnimation()
            } else {
                binding.frameSearch.visibility = View.VISIBLE
                resetPortraitSearchMotionOffsets()
                binding.searchText.clearFocus()
            }
        } else {
            binding.searchText.clearFocus()
        }
    }

    private fun focusSearch() {
        binding.searchText.requestFocus()
        val imm = requireActivity()
            .getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager?
        imm?.showSoftInput(binding.searchText, InputMethodManager.SHOW_IMPLICIT)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (_binding == null) {
            return
        }
        originalHeaderTopMargin = null
        originalHeaderLeftMargin = null
        originalHeaderRightMargin = null
        setGamesSearchExpanded(gamesSearchExpanded, animated = false)
        ViewCompat.requestApplyInsets(binding.root)
    }

    override fun onDestroyView() {
        gamesSearchExpanded = false
        super.onDestroyView()
        _binding = null
    }

    private fun scrollToTop() {
        if (_binding != null) {
            (binding.gridGames as? CarouselRecyclerView)?.smoothScrollToPosition(0)
        }
    }

    private fun launchQLaunch() {
        try {
            val appletPath = NativeLibrary.getAppletLaunchPath(AppletInfo.QLaunch.entryId)
            if (appletPath.isEmpty()) {
                Toast.makeText(
                    requireContext(),
                    R.string.applets_error_applet,
                    Toast.LENGTH_SHORT
                ).show()
                return
            }

            NativeLibrary.setCurrentAppletId(AppletInfo.QLaunch.appletId)

            val qlaunchGame = Game(
                title = getString(R.string.qlaunch_applet),
                path = appletPath
            )

            val action = HomeNavigationDirections.actionGlobalEmulationActivity(qlaunchGame)
            findNavController().navigate(action)
        } catch (e: Exception) {
            Toast.makeText(
                requireContext(),
                "Failed to launch QLaunch: ${e.message}",
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun updateButtonsVisibility() {
        val showQLaunch = BooleanSetting.ENABLE_QLAUNCH_BUTTON.getBoolean()
        val showFolder = BooleanSetting.ENABLE_FOLDER_BUTTON.getBoolean()
        val isFirmwareAvailable = NativeLibrary.isFirmwareAvailable()

        val shouldShowQLaunch = showQLaunch && isFirmwareAvailable
        binding.launchQlaunch.visibility = if (shouldShowQLaunch) View.VISIBLE else View.GONE

        binding.addDirectory.visibility = if (showFolder) View.VISIBLE else View.GONE
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val spacingNavigation = resources.getDimensionPixelSize(R.dimen.spacing_navigation)
            resources.getDimensionPixelSize(R.dimen.spacing_navigation_rail)

            (binding.swipeRefresh as? SwipeRefreshLayout)?.setProgressViewEndTarget(
                false,
                barInsets.top + resources.getDimensionPixelSize(R.dimen.spacing_refresh_end)
            )

            val leftInset = barInsets.left + cutoutInsets.left
            val rightInset = barInsets.right + cutoutInsets.right
            val topInset = maxOf(barInsets.top, cutoutInsets.top)

            val mlpSwipe = binding.swipeRefresh.layoutParams as ViewGroup.MarginLayoutParams
            mlpSwipe.leftMargin = leftInset
            mlpSwipe.rightMargin = rightInset
            binding.swipeRefresh.layoutParams = mlpSwipe

            val mlpHeader = binding.header.layoutParams as ViewGroup.MarginLayoutParams

            // Store original margins only once
            if (originalHeaderTopMargin == null) {
                originalHeaderTopMargin = mlpHeader.topMargin
                originalHeaderRightMargin = mlpHeader.rightMargin
                originalHeaderLeftMargin = mlpHeader.leftMargin
            }

            // Always set margin as original + insets
            mlpHeader.leftMargin = (originalHeaderLeftMargin ?: 0) + leftInset
            mlpHeader.rightMargin = (originalHeaderRightMargin ?: 0) + rightInset
            mlpHeader.topMargin = (originalHeaderTopMargin ?: 0) + topInset + resources.getDimensionPixelSize(
                R.dimen.spacing_med
            )
            binding.header.layoutParams = mlpHeader

            binding.noticeText.updatePadding(bottom = spacingNavigation)

            binding.gridGames.updatePadding(
                top = resources.getDimensionPixelSize(R.dimen.spacing_med)
            )

            val mlpFab = binding.addDirectory.layoutParams as ViewGroup.MarginLayoutParams
            val fabPadding = resources.getDimensionPixelSize(R.dimen.spacing_large)
            mlpFab.leftMargin = leftInset + fabPadding
            mlpFab.bottomMargin = barInsets.bottom + fabPadding
            mlpFab.rightMargin = rightInset + fabPadding
            binding.addDirectory.layoutParams = mlpFab

            binding.launchQlaunch?.let { qlaunchButton ->
                val mlpQLaunch = qlaunchButton.layoutParams as ViewGroup.MarginLayoutParams
                mlpQLaunch.leftMargin = leftInset + fabPadding
                mlpQLaunch.bottomMargin = barInsets.bottom + fabPadding
                qlaunchButton.layoutParams = mlpQLaunch
            }

            windowInsets
        }
}
