/* SPDX-License-Identifier: MIT */
#ifndef XZ_GAME_FEEL_H
#define XZ_GAME_FEEL_H
#define XZ_GAME_IMPACTS 8
/* Event time and world position are independent source-sample coordinates. */
enum xz_game_impact_kind { XZ_IMPACT_LAND=1,XZ_IMPACT_PERFECT_LAND,XZ_IMPACT_LAUNCH,XZ_IMPACT_BOOST,XZ_IMPACT_SHATTER,XZ_IMPACT_GLANCE,XZ_IMPACT_ARROW };
struct xz_game_impact { double time,at; float altitude,strength,angle; unsigned kind,serial; };
struct xz_game_effects { struct xz_game_impact events[XZ_GAME_IMPACTS]; unsigned serial; };
static inline void xz_game_emit(struct xz_game_effects *f,unsigned kind,double time,double at,float altitude,float strength,float angle){
 unsigned serial=++f->serial;struct xz_game_impact *e=&f->events[(serial-1)%XZ_GAME_IMPACTS];
 *e=(struct xz_game_impact){time,at,altitude,strength<0?0:strength>1?1:strength,angle,kind,serial};
}
#endif
