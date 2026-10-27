/* Native firmware-1.26 UI read fixtures, shared by observer and real runtime tests. */
static uint32_t browse,status,ids[2];
/* sCautionBrowseString / sCautionStatusString, UTF-16; all zero = no text. */
static uint16_t browse_text[32],status_text[32];
static inline void caution_text(uint16_t out[32],const char *ascii){memset(out,0,64);for(int i=0;ascii&&ascii[i]&&i<31;i++)out[i]=(unsigned char)ascii[i];}
static int failed,torn,calls;
static uint32_t failed_at,mismatch_at,torn_at;
static const unsigned char guards[3][16]={
 {0xf0,0x4f,0x2d,0xe9,0x04,0x8b,0x2d,0xed,0xfc,0xd0,0x4d,0xe2,0x59,0x6b,0xfe,0xeb},
 {0xf0,0x4f,0x2d,0xe9,0xa4,0xd0,0x4d,0xe2,0x55,0x67,0xfe,0xeb,0x00,0x00,0x50,0xe3},
 {0xf0,0x45,0x2d,0xe9,0x0c,0xd0,0x4d,0xe2,0x00,0x40,0xa0,0xe1,0x69,0x69,0x01,0xeb}};
int xz_read_memory(uint32_t at,void *out,size_t n){
 if(failed||at==failed_at)return -1;
 const void *p=NULL;
 if(at==0x146fcc&&n==16)p=guards[0];
 if(at==0x147f08&&n==16)p=guards[1];
 if(at==0xe1d80&&n==16)p=guards[2];
 if(at==0x1adae30&&n==4)p=&browse;
 if(at==0x1adaea4&&n==4)p=&status;
 if(at==0x222c668&&n==8)p=ids;
 if(at==0x1ada958&&n==64)p=browse_text;
 if(at==0x1ada718&&n==64)p=status_text;
 if(!p)return -1;memcpy(out,p,n);
 if(at==mismatch_at||(torn&&++calls%2)||(at==torn_at&&++calls%2))((unsigned char*)out)[0]^=1;
 return 0;
}
