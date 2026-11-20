// PS4: no native file dialog. Every dialog reports an error, so the callers fall back to their default paths.
#pragma once
#include <stddef.h>
typedef char nfdnchar_t;
typedef void nfdpathset_t;
typedef unsigned int nfdpathsetsize_t;
typedef struct { const nfdnchar_t* name; const nfdnchar_t* spec; } nfdnfilteritem_t;
typedef enum { NFD_ERROR, NFD_OKAY, NFD_CANCEL } nfdresult_t;
static inline nfdresult_t NFD_Init(void) { return NFD_OKAY; }
static inline void NFD_Quit(void) {}
static inline const char* NFD_GetError(void) { return "no file dialog on PS4"; }
static inline void NFD_FreePathN(nfdnchar_t*) {}
static inline nfdresult_t NFD_OpenDialogN(nfdnchar_t**, const nfdnfilteritem_t*, nfdpathsetsize_t, const nfdnchar_t*) { return NFD_ERROR; }
static inline nfdresult_t NFD_OpenDialogMultipleN(const nfdpathset_t**, const nfdnfilteritem_t*, nfdpathsetsize_t, const nfdnchar_t*) { return NFD_ERROR; }
static inline void NFD_PathSet_Free(const nfdpathset_t*) {}
static inline nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t*, nfdpathsetsize_t* n) { *n = 0; return NFD_OKAY; }
static inline nfdresult_t NFD_PathSet_GetPathN(const nfdpathset_t*, nfdpathsetsize_t, nfdnchar_t**) { return NFD_ERROR; }
