static bool ww_browser_accept_file(const char *name)
{
	if (g_browser_filter == WW_BROWSER_FILTER_NDS)
		return ww_name_has_nds_extension(name);

	return ww_name_has_sav_extension(name);
}

static void ww_selected_save_directory(char *out, size_t out_size)
{
	ww_selected_directory_from_path(g_selected_hgss_save_path, out, out_size);
}

static void ww_selected_rom_directory(char *out, size_t out_size)
{
	ww_selected_directory_from_path(g_selected_hgss_nds_path, out, out_size);
}

static int ww_browser_entry_cmp(const void *left, const void *right)
{
	const ww_browser_entry *a = (const ww_browser_entry *)left;
	const ww_browser_entry *b = (const ww_browser_entry *)right;

	if (a->is_dir != b->is_dir)
		return a->is_dir ? -1 : 1;

	return strcasecmp(a->name, b->name);
}

static bool ww_browser_reload(void)
{
	DIR *directory;
	struct dirent *entry;

	g_browser_entry_count = 0;
	directory = opendir(g_browser_cwd);
	if (!directory)
		return false;

	while ((entry = readdir(directory)) != NULL) {
		struct stat info;
		char full_path[WW_SAVE_PATH_MAX];
		bool is_dir;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		ww_path_join(g_browser_cwd, entry->d_name, full_path, sizeof(full_path));
		if (stat(full_path, &info) != 0)
			continue;

		is_dir = S_ISDIR(info.st_mode);
		if (!is_dir && !ww_browser_accept_file(entry->d_name))
			continue;

		if (g_browser_entry_count >= WW_BROWSER_MAX_ENTRIES)
			break;

		snprintf(g_browser_entries[g_browser_entry_count].name,
				sizeof(g_browser_entries[g_browser_entry_count].name),
				"%s", entry->d_name);
		g_browser_entries[g_browser_entry_count].is_dir = is_dir;
		g_browser_entry_count++;
	}

	closedir(directory);

	if (g_browser_entry_count > 1)
		qsort(g_browser_entries, g_browser_entry_count, sizeof(g_browser_entries[0]), ww_browser_entry_cmp);

	if (g_browser_entry_count == 0)
		g_browser_selected = 0;
	else if (g_browser_selected >= (s32)g_browser_entry_count)
		g_browser_selected = (s32)g_browser_entry_count - 1;

	if (g_browser_selected < 0)
		g_browser_selected = 0;

	g_browser_first = 0;
	return true;
}

static void ww_browser_open_for_filter(ww_browser_filter filter)
{
	char start_dir[WW_SAVE_PATH_MAX];

	g_browser_filter = filter;
	if (filter == WW_BROWSER_FILTER_NDS) {
		ww_selected_rom_directory(start_dir, sizeof(start_dir));
		if (ww_path_is_sd_root(start_dir))
			ww_selected_save_directory(start_dir, sizeof(start_dir));
	} else {
		ww_selected_save_directory(start_dir, sizeof(start_dir));
	}

	snprintf(g_browser_cwd, sizeof(g_browser_cwd), "%s", start_dir);
	g_browser_selected = 0;
	g_browser_first = 0;

	if (!ww_browser_reload()) {
		snprintf(g_browser_cwd, sizeof(g_browser_cwd), "sdmc:/");
		if (!ww_browser_reload())
			printf("Failed to open SD browser\n");
	}
}

static void ww_browser_move_selection(s32 offset)
{
	s32 selected;

	if (g_browser_entry_count == 0) {
		g_browser_selected = 0;
		return;
	}

	selected = g_browser_selected + offset;
	if (selected < 0)
		selected = 0;
	if (selected >= (s32)g_browser_entry_count)
		selected = (s32)g_browser_entry_count - 1;

	g_browser_selected = selected;
}

static void ww_browser_enter_parent(void)
{
	if (ww_path_is_sd_root(g_browser_cwd))
		return;

	ww_path_parent(g_browser_cwd, sizeof(g_browser_cwd));
	g_browser_selected = 0;
	g_browser_first = 0;
	ww_browser_reload();
}

static bool ww_browser_activate_selected(void)
{
	char path[WW_SAVE_PATH_MAX];
	ww_browser_entry *entry;

	if (g_browser_entry_count == 0 || g_browser_selected < 0 || g_browser_selected >= (s32)g_browser_entry_count)
		return false;

	entry = &g_browser_entries[g_browser_selected];
	ww_path_join(g_browser_cwd, entry->name, path, sizeof(path));

	if (entry->is_dir) {
		snprintf(g_browser_cwd, sizeof(g_browser_cwd), "%s", path);
		g_browser_selected = 0;
		g_browser_first = 0;
		return ww_browser_reload();
	}

	if (g_browser_filter == WW_BROWSER_FILTER_NDS)
		snprintf(g_selected_hgss_nds_path, sizeof(g_selected_hgss_nds_path), "%s", path);
	else
		snprintf(g_selected_hgss_save_path, sizeof(g_selected_hgss_save_path), "%s", path);

	ww_save_ui_config();
	return true;
}

