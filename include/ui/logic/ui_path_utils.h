#ifndef WW_UI_PATH_UTILS_H
#define WW_UI_PATH_UTILS_H

#include <stdbool.h>
#include <stddef.h>

bool ww_path_is_sd_root(const char *path);
bool ww_name_has_extension(const char *name, const char *extension);
bool ww_name_has_sav_extension(const char *name);
bool ww_name_has_nds_extension(const char *name);
void ww_path_join(const char *directory, const char *name, char *out, size_t out_size);
void ww_path_parent(char *path, size_t path_size);
void ww_selected_directory_from_path(const char *selected_path, char *out, size_t out_size);

#endif
