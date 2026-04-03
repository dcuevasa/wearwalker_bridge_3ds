#include "ui/logic/ui_path_utils.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

bool ww_path_is_sd_root(const char *path)
{
	return path && (strcmp(path, "sdmc:/") == 0 || strcmp(path, "sdmc:") == 0);
}

bool ww_name_has_extension(const char *name, const char *extension)
{
	const char *dot;

	if (!name || !extension)
		return false;

	dot = strrchr(name, '.');
	if (!dot)
		return false;

	return strcasecmp(dot, extension) == 0;
}

bool ww_name_has_sav_extension(const char *name)
{
	return ww_name_has_extension(name, ".sav");
}

bool ww_name_has_nds_extension(const char *name)
{
	return ww_name_has_extension(name, ".nds");
}

void ww_path_join(const char *directory, const char *name, char *out, size_t out_size)
{
	const char *entry_name = name ? name : "";
	size_t len;

	if (!out || out_size == 0)
		return;

	if (!directory || !directory[0] || ww_path_is_sd_root(directory))
		snprintf(out, out_size, "sdmc:/%s", entry_name);
	else {
		snprintf(out, out_size, "%s", directory);
		len = strlen(out);
		if (len + 1 < out_size) {
			out[len] = '/';
			out[len + 1] = '\0';
		}
		strncat(out, entry_name, out_size - strlen(out) - 1);
	}
}

void ww_path_parent(char *path, size_t path_size)
{
	char *last_slash;
	size_t len;

	if (!path || path_size == 0)
		return;

	if (ww_path_is_sd_root(path)) {
		snprintf(path, path_size, "sdmc:/");
		return;
	}

	len = strlen(path);
	while (len > 6 && path[len - 1] == '/') {
		path[len - 1] = '\0';
		len--;
	}

	last_slash = strrchr(path, '/');
	if (!last_slash || (size_t)(last_slash - path) <= 5) {
		snprintf(path, path_size, "sdmc:/");
		return;
	}

	*last_slash = '\0';
}

void ww_selected_directory_from_path(const char *selected_path, char *out, size_t out_size)
{
	char *last_slash;

	if (!out || out_size == 0)
		return;

	if (!selected_path || !selected_path[0]) {
		snprintf(out, out_size, "sdmc:/");
		return;
	}

	snprintf(out, out_size, "%s", selected_path);
	last_slash = strrchr(out, '/');
	if (!last_slash || (size_t)(last_slash - out) <= 5)
		snprintf(out, out_size, "sdmc:/");
	else
		*last_slash = '\0';
}
