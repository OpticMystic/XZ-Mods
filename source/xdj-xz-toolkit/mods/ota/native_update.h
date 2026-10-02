/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_UPDATE_H
#define XZ_NATIVE_UPDATE_H
enum xz_update_phase {XZ_UPDATE_IDLE,XZ_UPDATE_WORKING,XZ_UPDATE_AVAILABLE,XZ_UPDATE_CURRENT,XZ_UPDATE_STAGED,XZ_UPDATE_ERROR};
struct xz_update_model {enum xz_update_phase phase;int ready;char status[128];};
void xz_update_read(struct xz_update_model *);
void xz_update_request(int operation);
#endif
