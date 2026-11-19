// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.fragments

import android.os.Bundle
import androidx.fragment.app.Fragment
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isVisible
import androidx.core.view.updatePadding
import androidx.fragment.app.activityViewModels
import androidx.recyclerview.widget.LinearLayoutManager
import com.fasterxml.jackson.databind.JsonNode
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.transition.MaterialSharedAxis
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.FragmentDriverFetcherBinding
import org.yuzu.yuzu_emu.features.fetcher.DriverGroupAdapter
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import java.io.IOException
import java.util.concurrent.TimeUnit
import org.yuzu.yuzu_emu.model.DriverViewModel
import org.yuzu.yuzu_emu.model.HomeViewModel
import org.yuzu.yuzu_emu.utils.GpuDriverHelper
import org.yuzu.yuzu_emu.utils.ViewUtils.updateMargins
import java.net.URL
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import kotlin.getValue

class DriverFetcherFragment : Fragment() {

    companion object {
        private const val MAX_RELEASE_PAGES = 15
        private const val RELEASE_USER_AGENT = "Eden-Android-DriverFetcher"
    }
    private var _binding: FragmentDriverFetcherBinding? = null
    private val binding get() = _binding!!

    private var fetchSession = 0
    private var driversJobActive = false

    private val releaseHttpClient = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .build()

    enum class SortMode {
        Default, PublishTime,
    }

    private data class DriverRepo(
        val name: String = "",
        val path: String = "",
        val sort: Int = 0,
        val useTagName: Boolean = false,
        val sortMode: SortMode = SortMode.Default,
    )

    private val repoList: List<DriverRepo> = listOf(
        DriverRepo("Mr. Purple Turnip", "MrPurple666/purple-turnip", 0),
        DriverRepo("GameHub Adreno 8xx", "crueter/GameHub-8Elite-Drivers", 1),
        DriverRepo("KIMCHI Turnip", "K11MCH1/AdrenoToolsDrivers", 2, true, SortMode.PublishTime),
        DriverRepo("Weab-Chan Freedreno", "Weab-chan/freedreno_turnip-CI", 3),
        DriverRepo("Whitebelyash Turnip", "whitebelyash/freedreno_turnip-CI", sort=4, false, SortMode.PublishTime),
    )

    private val driverMap = listOf(
        IntRange(Integer.MIN_VALUE, 9) to "Unsupported",
        IntRange(10, 99) to "KIMCHI Latest",
        IntRange(100, 599) to "Unsupported",
        IntRange(600, 639) to "Mr. Purple EOL-24.3.4",
        IntRange(640, 699) to "Mr. Purple T19",
        IntRange(700, 710) to "Mr. Purple T29",
        IntRange(711, 899) to "Mr. Purple T30",
        IntRange(900, Int.MAX_VALUE) to "Unsupported",
    )

    private lateinit var driverGroupAdapter: DriverGroupAdapter
    private val homeViewModel: HomeViewModel by activityViewModels()
    private val driverViewModel: DriverViewModel by activityViewModels()

    private fun recommendedDriverFor(gpuModel: String?): String {
        val adrenoModel = parseAdrenoModel(gpuModel)
        return driverMap.firstOrNull { adrenoModel in it.first }?.second ?: "Unsupported"
    }

    private fun parseAdrenoModel(gpuModel: String?): Int {
        if (gpuModel == null) {
            return 0
        }

        val modelList = gpuModel.split(" ")
        if (modelList.size < 3 || modelList[0] != "Adreno") {
            return 0
        }

        val model = modelList[2]
        return try {
            if (model.startsWith("A")) {
                model.substring(1).toInt()
            } else {
                model.toInt()
            }
        } catch (_: Exception) {
            0
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentDriverFetcherBinding.inflate(inflater)
        val gpuModel = GpuDriverHelper.tryGetGpuModel()
        binding.cardDeviceSummary.badgeRecommendedDriver.text = recommendedDriverFor(gpuModel)
        binding.cardDeviceSummary.badgeGpuModel.text = gpuModel ?: getString(R.string.unknown)

        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setStatusBarShadeVisibility(visible = false)
        binding.toolbarDrivers.setNavigationOnClickListener {
            requireActivity().onBackPressedDispatcher.onBackPressed()
        }

        binding.scrollDriverFetcher.isNestedScrollingEnabled = true
        binding.scrollDriverFetcher.isSmoothScrollingEnabled = true
        binding.listDrivers.layoutManager = LinearLayoutManager(context)
        binding.listDrivers.isNestedScrollingEnabled = false
        binding.listDrivers.setHasFixedSize(false)
        binding.listDrivers.itemAnimator = null
        driverGroupAdapter = DriverGroupAdapter(
            requireActivity(),
            driverViewModelProvider = { driverViewModel },
        )
        binding.listDrivers.adapter = driverGroupAdapter

        setInsets()

        fetchDrivers()
    }

    override fun onDestroyView() {
        driversJobActive = false
        fetchSession++
        super.onDestroyView()
        _binding = null
    }

    private fun driverGroup(
        driver: DriverRepo,
        releases: ArrayList<Release>,
    ): DriverGroup =
        DriverGroup(
            name = driver.name,
            sort = driver.sort,
            repoPath = driver.path,
            useTagName = driver.useTagName,
            sortMode = driver.sortMode,
            releases = releases,
        )

    private fun showFetchErrorDialog(repoName: String, message: String?) {
        if (!isAdded) {
            return
        }
        MaterialAlertDialogBuilder(requireActivity())
            .setTitle(getString(R.string.error_during_fetch))
            .setMessage("${getString(R.string.failed_to_fetch)} $repoName:\n$message")
            .setPositiveButton(getString(R.string.ok)) { dialog, _ -> dialog.cancel() }
            .show()
    }

    private fun fetchDrivers() {
        val binding = _binding ?: return
        binding.loadingIndicator.isVisible = true
        if (::driverGroupAdapter.isInitialized) {
            driverGroupAdapter.updateDriverGroups(emptyList())
        }

        val allRepos = repoList
        val session = ++fetchSession
        driversJobActive = true
        val driverGroups = arrayListOf<DriverGroup>()

        fun mergeGroup(group: DriverGroup) {
            synchronized(driverGroups) {
                val key = group.repoPath.lowercase()
                val index = driverGroups.indexOfFirst {
                    it.repoPath.lowercase() == key
                }
                if (index >= 0) {
                    driverGroups[index] = group
                } else {
                    driverGroups.add(group)
                }
                driverGroups.sortBy { it.sort }
            }
        }

        fun publishGroups() {
            val binding = _binding ?: return
            if (!driversJobActive || session != fetchSession) {
                return
            }
            driverGroupAdapter.updateDriverGroups(driverGroups.toList())
            if (driverGroups.size >= allRepos.size) {
                binding.loadingIndicator.isVisible = false
            }
        }

        allRepos.forEach { driver ->
            viewLifecycleOwner.lifecycleScope.launch {
                if (!driversJobActive || session != fetchSession) {
                    return@launch
                }

                var releases = arrayListOf<Release>()
                var fetchFailed = false
                try {
                    releases = fetchGitHubReleases(
                        driver.path,
                        driver.useTagName,
                        driver.sortMode,
                    )
                } catch (e: Exception) {
                    fetchFailed = true
                    withContext(Dispatchers.Main) {
                        if (isAdded && driversJobActive && session == fetchSession) {
                            showFetchErrorDialog(driver.name, e.message)
                        }
                    }
                }

                if (!driversJobActive || session != fetchSession || _binding == null) {
                    return@launch
                }

                if (!fetchFailed || releases.isNotEmpty()) {
                    mergeGroup(driverGroup(driver, releases))
                    publishGroups()
                }
            }
        }
    }

    private suspend fun fetchGitHubReleases(
        repoPath: String,
        useTagName: Boolean,
        sortMode: SortMode,
    ): ArrayList<Release> = withContext(Dispatchers.IO) {
        Release.fromJsonArray(fetchGitHubReleasesJson(repoPath), useTagName, sortMode)
    }

    private fun fetchGitHubReleasesJson(repoPath: String): String {
        val merged = JSONArray()
        var page = 1
        while (page <= MAX_RELEASE_PAGES) {
            val request = Request.Builder()
                .url(
                    "https://api.github.com/repos/$repoPath/releases?per_page=100&page=$page",
                )
                .header("Accept", "application/vnd.github+json")
                .header("User-Agent", RELEASE_USER_AGENT)
                .build()
            val body = releaseHttpClient.newCall(request).execute().use { response ->
                if (!response.isSuccessful) {
                    throw IOException("HTTP ${response.code} for $repoPath page $page")
                }
                response.body?.string().orEmpty()
            }
            if (body.isBlank()) {
                break
            }
            val pageArray = JSONArray(body)
            if (pageArray.length() == 0) {
                break
            }
            for (i in 0 until pageArray.length()) {
                merged.put(pageArray.getJSONObject(i))
            }
            if (pageArray.length() < 100) {
                break
            }
            page++
        }
        return merged.toString()
    }

    private fun setInsets() {
        val root = _binding?.root ?: return
        ViewCompat.setOnApplyWindowInsetsListener(root) { _: View, windowInsets: WindowInsetsCompat ->
            val binding = _binding ?: return@setOnApplyWindowInsetsListener windowInsets
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.toolbarDrivers.updateMargins(left = leftInsets, right = rightInsets)
            binding.scrollDriverFetcher.updatePadding(
                left = leftInsets,
                right = rightInsets,
                bottom = barInsets.bottom + resources.getDimensionPixelSize(R.dimen.spacing_large),
            )

            windowInsets
        }
    }

    data class Artifact(val url: URL, val name: String)

    data class Release(
        var tagName: String = "",
        var titleName: String = "",
        var title: String = "",
        var body: String = "",
        var artifacts: List<Artifact> = ArrayList(),
        var prerelease: Boolean = false,
        var latest: Boolean = false,
        var publishTime: LocalDateTime = LocalDateTime.now()
    ) {
        companion object {
            fun fromJsonArray(
                jsonString: String,
                useTagName: Boolean,
                sortMode: SortMode
            ): ArrayList<Release> {
                val mapper = jacksonObjectMapper()

                try {
                    val rootNode = mapper.readTree(jsonString)

                    val releases = ArrayList<Release>()

                    var latestRelease: Release? = null

                    if (rootNode.isArray) {
                        rootNode.forEach { node ->
                            val release = fromJson(node, useTagName)

                            if (latestRelease == null && !release.prerelease) {
                                latestRelease = release
                                release.latest = true
                            }

                            releases.add(release)
                        }
                    }

                    when (sortMode) {
                        SortMode.PublishTime -> releases.sortByDescending {
                            it.publishTime
                        }

                        else -> {}
                    }

                    return releases
                } catch (e: Exception) {
                    e.printStackTrace()
                    return ArrayList()
                }
            }

            private fun fromJson(node: JsonNode, useTagName: Boolean): Release {
                return try {
                    val tagName = node.path("tag_name").asText("")
                    val body = node.path("body").asText("")
                    val prerelease = node.path("prerelease").asBoolean(false)
                    val titleName = node.path("name").asText(tagName)

                    val published = node.path("published_at").asText("")
                    val localTime = try {
                        if (published.isNotBlank()) {
                            Instant.parse(published)
                                .atZone(ZoneId.systemDefault())
                                .toLocalDateTime()
                        } else {
                            LocalDateTime.now()
                        }
                    } catch (_: Exception) {
                        LocalDateTime.now()
                    }

                    val title = if (useTagName) tagName else titleName

                    val artifacts = ArrayList<Artifact>()
                    val assets = node.path("assets")
                    if (assets.isArray) {
                        assets.forEach { subNode ->
                            val urlStr = subNode.path("browser_download_url").asText("")
                            val name = subNode.path("name").asText("")
                            if (urlStr.isNotBlank() && name.isNotBlank()) {
                                artifacts.add(Artifact(URL(urlStr), name))
                            }
                        }
                    }

                    Release(
                        tagName,
                        titleName,
                        title,
                        body,
                        artifacts,
                        prerelease,
                        false,
                        localTime,
                    )
                } catch (_: Exception) {
                    Release()
                }
            }
        }
    }

    data class DriverGroup(
        val name: String,
        val sort: Int,
        val repoPath: String,
        val useTagName: Boolean = false,
        val sortMode: SortMode = SortMode.Default,
        val releases: ArrayList<Release> = arrayListOf(),
    )
}
