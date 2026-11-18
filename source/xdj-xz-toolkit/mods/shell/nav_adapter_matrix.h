/* Local runtime outcomes only; recovery input still goes through the real handlers. */
static const char *const nav_notices[]={"DECK 1 DID NOT LOAD THE TRACK","THE TRACK ON USB 2 COULD NOT BE READ BECAUSE THE DEVICE WAS REMOVED WHILE IT WAS LOADING","USB1 IS NOT CONNECTED, USB2 IS NOT CONNECTED","USB SOURCE CHANGED: SELECT TRACK AGAIN","SELECT A TRACK, THEN PRESS LOAD","OPEN A TRACK LIST TO LOAD, NO USB TRACK LIST TO LOAD FROM","LOAD FROM THIS LIST IS OFF","THIS ROW CANNOT BE LOADED, THAT DECK CANNOT LOAD THIS TRACK","TAG LIST IS FULL: 100 TRACKS","MATCHING NEEDS A MASTER DECK","NO MASTER DECK: BPM FILTER OFF, NO MASTER KEY: KEY FILTER OFF","COMPUTER BROWSING IS NOT AVAILABLE YET, LINK BROWSING IS NOT AVAILABLE YET","LINK DID NOT OPEN, EXPORT MODE DID NOT OPEN","MODS DID NOT OPEN: PUSH AGAIN","TOUCH CANNOT OPEN THIS YET: PUSH THE ENCODER","THAT SETTING CANNOT CHANGE HERE","DECK 3 AND 4 ARE NOT RUNNING","KEY SHIFT REFUSED: AUDIO LOAD TOO HIGH","THIS DECK IS MASTER","A player warning such as E-8307: USB ACCESS ERROR","THIS DJ PROFILE CANNOT BE READ"};
static int nav_matrix_fixture(int code){
 if(code>=500&&code<500+(int)(sizeof nav_notices/sizeof *nav_notices)){sm.notice=nav_notices[code-500];sm.notice_at_ms=now;return 1;}
 if(code==599){sm.notice=NULL;return 1;}
 if(code==610||code==611){dj.count=2;dj.entry[0]=(struct xz_ui_dj_entry){.name="LEGACY DJ",.tagline="LOCKED CARD",.usb=0,.locked=1};dj.entry[1]=(struct xz_ui_dj_entry){.name="DAMAGED PROFILE",.usb=1,.damaged=1};dj.current=code==610?0:1;return 1;}
 return 0;
}
