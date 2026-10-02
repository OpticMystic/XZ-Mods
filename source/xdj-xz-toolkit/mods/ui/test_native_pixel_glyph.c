/* SPDX-License-Identifier: MIT */
#include "native_pixel_glyph.h"
#include <assert.h>
#include <stdio.h>

#ifdef NDEBUG
#error "Native glyph acceptance requires active assertions"
#endif

static unsigned pixel(const uint8_t *raster,unsigned stride,unsigned x,unsigned y)
{ return (raster[y*stride+x/4]>>(6-(x%4)*2))&3; }

static unsigned expected_pixel(unsigned ch,unsigned encoding,unsigned width,unsigned height,unsigned x,unsigned y)
{
    unsigned first=5,last=0;
    for(unsigned c=0;c<5;c++) for(unsigned r=0;r<7;r++)
        if(xz_pixel_font_row(ch,r)&(16u>>c)) { if(c<first)first=c; if(c>last)last=c; }
    if(first==5) return 0;
    unsigned sx,sy,columns;
    if(encoding==2) { first=0; columns=5; sx=sy=2; }
    else { columns=last-first+1; sy=3; sx=width/columns<3?width/columns:3; }
    unsigned left=(width-columns*sx)/2,top=(height-7*sy)/2;
    if(x<left||x>=left+columns*sx||y<top||y>=top+7*sy) return 0;
    return (xz_pixel_font_row(ch,(y-top)/sy)>>(4-first-(x-left)/sx))&1?3:0;
}

struct extent { unsigned x0,x1,y0,y1; };
static struct extent ink_extent(const struct xz_native_text_bitmap *b)
{
    struct extent e={~0u,0,~0u,0};
    for(unsigned y=0;y<b->height;y++) for(unsigned x=0;x<b->stride*4;x++) if(pixel(b->data,b->stride,x,y)) {
        if(x<e.x0)e.x0=x; if(x+1>e.x1)e.x1=x+1; if(y<e.y0)e.y0=y; if(y+1>e.y1)e.y1=y+1;
    }
    return e;
}

int main(void)
{
    uint8_t storage[191];
    struct xz_native_text_bitmap bitmap={storage+1,189,2,-3,21,27,7};
    struct xz_native_text_glyph_scope scope={1,1,0x206fac,1,storage+1,189};
    const unsigned encodings[]={1,2,4,14,16};
    unsigned accepted=0;
    for(unsigned encoding=0;encoding<5;encoding++)
    for(unsigned width=5;width<=28;width++) for(unsigned ch=0;ch<0x200;ch++) {
        struct xz_native_text_bitmap b=bitmap;
        b.width=width;
        if(encodings[encoding]==2) { if(width!=11) continue; b.height=23; b.stride=3; }
        memset(storage,0x5a,sizeof(storage));
        uint8_t untouched[191]; memcpy(untouched,storage,sizeof(untouched));
        struct xz_native_text_bitmap before=b;
        int result=xz_native_text_pixel_glyph(&scope,0,ch,encodings[encoding],&b);
        assert(!memcmp(&b,&before,sizeof(b)));
        if(!xz_pixel_font_has(ch)) {
            assert(!result); assert(!memcmp(storage,untouched,sizeof(storage)));
            continue;
        }
        assert(result==1); accepted++;
        assert(storage[0]==0x5a);
        for(size_t i=1+b.stride*b.height;i<sizeof(storage);i++) assert(storage[i]==0x5a);
        for(unsigned y=0;y<b.height;y++) for(unsigned x=0;x<b.stride*4;x++)
            assert(pixel(b.data,b.stride,x,y)==expected_pixel(ch,encodings[encoding],b.width,b.height,x,y));
        uint8_t first[191]; memcpy(first,storage,sizeof(first));
        assert(xz_native_text_pixel_glyph(&scope,0,ch,encodings[encoding],&b)==1);
        assert(!memcmp(first,storage,sizeof(first)));
    }
    assert(accepted==(95+62)*(4*24+1));
    for(unsigned encoding=0;encoding<5;encoding++) {
        struct xz_native_text_bitmap b=bitmap;
        if(encodings[encoding]==2) { b.width=11; b.height=23; b.stride=3; }
        memset(storage,0x5a,sizeof(storage));
        assert(xz_native_text_pixel_glyph(&scope,0,' ',encodings[encoding],&b)==1);
        for(size_t i=0;i<(size_t)b.stride*b.height;i++) assert(b.data[i]==0);
    }
    {
        struct xz_native_text_bitmap b=bitmap;
        assert(xz_native_text_pixel_glyph(&scope,0,'M',1,&b)==1);
        struct extent m=ink_extent(&b);
        assert(xz_native_text_pixel_glyph(&scope,0,'i',1,&b)==1);
        struct extent i=ink_extent(&b);
        assert(m.y0==3&&m.y1==24&&i.y0==m.y0&&i.y1==m.y1);
        assert(m.x1-m.x0==15&&i.x1-i.x0==9);
        b.width=5;
        assert(xz_native_text_pixel_glyph(&scope,0,'i',16,&b)==1);
        i=ink_extent(&b);
        assert(i.y0==3&&i.y1==24&&i.x0==1&&i.x1==4);
        b.width=11; b.height=23; b.stride=3;
        assert(xz_native_text_pixel_glyph(&scope,0,'M',2,&b)==1);
        m=ink_extent(&b);
        assert(m.x0==0&&m.x1==10&&m.y0==4&&m.y1==18);
        assert(xz_native_text_pixel_glyph(&scope,0,'i',2,&b)==1);
        i=ink_extent(&b);
        assert(i.x0==2&&i.x1==8&&i.y0==4&&i.y1==18);
    }
    /* Every eligibility field independently fails closed with unchanged raster. */
    for(unsigned rejection=0;rejection<22;rejection++) {
        struct xz_native_text_bitmap b=bitmap;
        struct xz_native_text_glyph_scope s=scope;
        unsigned result=0,ch='A',encoding=1;
        switch(rejection) {
        case 0:s.enabled=0;break;
        case 1:s.inside_wstring=0;break;
        case 2:s.backend++;break;
        case 3:s.packing_exponent=0;break;
        case 4:s.packing_exponent=2;break;
        case 5:s.supplied_data++;break;
        case 6:s.supplied_capacity--;break;
        case 7:b.capacity=s.supplied_capacity=188;break;
        case 8:b.width=4;break;
        case 9:b.width=29;break;
        case 10:b.height=26;break;
        case 11:b.stride=6;break;
        case 12:ch=31;break;
        case 13:ch=127;break;
        case 14:ch=0x65e5;break;
        case 15:encoding=3;break;
        case 16:encoding=2;break;
        case 17:result=1;break;
        case 18:b.data=NULL;break;
        case 19:ch=0x152;break;
        case 20:ch=0xD7;break;
        case 21:ch=0xA0;break;
        }
        memset(storage,0x5a,sizeof(storage));
        uint8_t before[191];memcpy(before,storage,sizeof(before));
        assert(!xz_native_text_pixel_glyph(&s,result,ch,encoding,&b));
        assert(!memcmp(storage,before,sizeof(storage)));
    }
    assert(!xz_native_text_pixel_glyph(NULL,0,'A',1,&bitmap));
    assert(!xz_native_text_pixel_glyph(&scope,0,'A',1,NULL));
    puts("native pixel glyph guarded raster replacement PASS");
    return 0;
}
