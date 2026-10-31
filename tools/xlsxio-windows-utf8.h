/* Pentarnion Studio integration patch for XLSXIO 0.2.37 (MIT).
 * Windows filenames supplied to XLSXIO are UTF-8, not the active ANSI code page.
 * The native adapter uses xlsxioread_open_memory for its read path. */
#if defined(_WIN32) && defined(USE_MINIZIP) && !defined(USE_MINIZIP_NG)
#include <minizip/iowin32.h>
#include <wchar.h>
static wchar_t* penta_xlsxio_wide_filename(const char* filename) {
  int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1, NULL, 0);
  wchar_t* wide;
  if (length <= 0 || (wide = (wchar_t*)malloc((size_t)length * sizeof(wchar_t))) == NULL)
    return NULL;
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1, wide, length) <= 0) {
    free(wide);
    return NULL;
  }
  return wide;
}
static int penta_xlsxio_unlink(const char* filename) {
  wchar_t* wide = penta_xlsxio_wide_filename(filename);
  int result;
  if (!wide) return -1;
  result = _wunlink(wide);
  free(wide);
  return result;
}
static zipFile penta_xlsxio_zip_open(const char* filename) {
  wchar_t* wide = penta_xlsxio_wide_filename(filename);
  zlib_filefunc64_def callbacks;
  zipFile result;
  if (!wide) return NULL;
  fill_win32_filefunc64W(&callbacks);
  result = zipOpen2_64(wide, APPEND_STATUS_CREATE, NULL, &callbacks);
  free(wide);
  return result;
}
#else
#define penta_xlsxio_unlink unlink
#define penta_xlsxio_zip_open(filename) zipOpen((filename), 0)
#endif
