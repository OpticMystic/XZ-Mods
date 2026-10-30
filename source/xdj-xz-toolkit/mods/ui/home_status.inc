/* Explicit renderer-owned selection from typed model state, never runtime text matching.
   Returned handles live in immutable atlas storage. Unknown/composed states retain
   the source buffer; no heap, frame scratch, provider, or settings access. */
static const char *xz_l10n_home_fixed(const struct xz_ui_model *m,int section,const char *fallback){
 const char *key=NULL;
 if(section==XZ_UI_STEM_SECTION){
  if(!xz_ui_usb_engine(m))key="Not set up";
  else if(!(m->enabled&XZ_UI_STEM))key="Off";
 }else if(section==XZ_UI_EXTRAS&&m->sdk.snapshot.count==0)key="6 games  |  no USB apps";
 else if(section==XZ_UI_FOUR_DECKS)switch(m->route.state){
 case XZ_UI_ROUTE_NO_ENGINE:key="Not available on this player";break;
 case XZ_UI_ROUTE_SCANNING:key="Looking for the cable";break;
 case XZ_UI_ROUTE_STARTING:case XZ_UI_ROUTE_WAITING_NATIVE_STOP:key="Starting decks 3 and 4";break;
 case XZ_UI_ROUTE_FAILED:key="Could not start";break;
 case XZ_UI_ROUTE_LOST:key="Disconnected";break;
 case XZ_UI_ROUTE_READY:break;
 default:key="Not connected";break;
 }
 return key?xz_l10n_authored(key):fallback;
}
