/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dj_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#x);failures++;}}while(0)

static char *slurp(const char *path,size_t *n){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    size_t cap=1<<20;char *data=malloc(cap+1);*n=data?fread(data,1,cap+1,f):0;fclose(f);
    if(!data||*n>cap){free(data);return NULL;}data[*n]=0;return data;
}
static int spit(const char *path,const char *data,size_t n){
    FILE *f=fopen(path,"wb");if(!f)return -1;int ok=fwrite(data,1,n,f)==n;return fclose(f)||!ok?-1:0;
}
static char *at(const char *root,const char *relative){
    static char buffers[4][4096];static int next;char *out=buffers[next++&3];
    snprintf(out,4096,"%s/%s",root,relative);return out;
}
static uint16_t rgb565(unsigned r,unsigned g,unsigned b){return (uint16_t)((r>>3)<<11|(g>>2)<<5|b>>3);}
static int solid(const struct xz_dj_profile *p,uint16_t value){
    for(int i=0;i<XZ_DJ_THUMB*XZ_DJ_THUMB;i++)if(p->thumb[i]!=value)return 0;
    return 1;
}

#define KAI_JSON "DJPROFILE/VJTOOLS/profiles/djp_kai/profile.json"
#define LENA_JSON "DJPROFILE/VJTOOLS/profiles/djp_lena/profile.json"
#define KAI_TILE "DJPROFILE/VJTOOLS/profiles/djp_kai/bundles/0b7c1f2e-0000-4000-8000-00000000c0de/renditions/xz-tile-160.rgb565"
#define INDEX_JSON "DJPROFILE/VJTOOLS/index.json"
#define MUSIC "PIONEER/rekordbox/export.pdb"

static void check_fixture(const struct xz_dj_stick *s){
    CHECK(s->count==2);CHECK(s->active==1);CHECK(!s->legacy);
    const struct xz_dj_profile *kai=s->profile,*lena=s->profile+1;
    CHECK(!strcmp(kai->dj_id,"djp_kai"));CHECK(!strcmp(kai->name,"DJ Kai"));CHECK(!strcmp(kai->tagline,"Deep tech, Berlin"));
    CHECK(kai->has_accent&&kai->accent==0xff2a6d);CHECK(!strcmp(kai->page_url,"https://vj.tools/dj#p1.fixture"));
    CHECK(kai->has_thumb&&solid(kai,rgb565(255,42,109)));CHECK(!kai->damaged);
    CHECK(!strcmp(lena->dj_id,"djp_lena"));CHECK(!strcmp(lena->name,"Lena B"));CHECK(!strcmp(lena->tagline,""));
    CHECK(!lena->has_accent);CHECK(!strcmp(lena->page_url,""));
    CHECK(lena->has_thumb&&solid(lena,rgb565(40,200,255)));CHECK(!lena->damaged);
    CHECK(!strcmp(kai->document_path,KAI_JSON));CHECK(!strcmp(lena->document_path,LENA_JSON));CHECK(!strcmp(kai->tile_path,KAI_TILE));
    CHECK(!strcmp(lena->tile_path,"DJPROFILE/VJTOOLS/profiles/djp_lena/bundles/5e1a9d40-0000-4000-8000-0000000b2b00/renditions/xz-tile-160.rgb565"));
}
static int stick(const char *root){
    static struct xz_dj_stick s;size_t n,m;
    char *music=slurp(at(root,MUSIC),&n);CHECK(music!=NULL);
    CHECK(xz_dj_stick_read(root,&s)==0);check_fixture(&s);

    /* Switch, then confirm only active/revision changed and the result still validates. */
    char *before=slurp(at(root,INDEX_JSON),&m);CHECK(before!=NULL);
    CHECK(xz_dj_stick_set_active(root,"djp_kai")==0);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==2);CHECK(s.active==0);
    size_t k;char *after=slurp(at(root,INDEX_JSON),&k);CHECK(after!=NULL);
    if(after){
        CHECK(xz_dj_index_valid(after,k)==0);
        CHECK(strstr(after,"\"profiles\"")&&strstr(after,"\"dj_id\": \"djp_lena\"")&&strstr(after,"\"dj_name\": \"Lena B\""));
        CHECK(strstr(after,"\"active\": \"djp_kai\""));CHECK(!strstr(after,"2026-10-08T15-00-00Z-0001"));
        CHECK(k+1==m);/* "djp_lena" -> "djp_kai", revision same length: nothing else moved */
        const char *r=strstr(after,"\"revision\": \"");CHECK(r!=NULL);
        if(r){
            r+=13;int good=strlen(r)>25&&r[25]=='"'&&r[4]=='-'&&r[7]=='-'&&r[10]=='T'&&r[13]=='-'&&r[16]=='-'&&r[19]=='Z'&&r[20]=='-';
            for(int i=21;i<25;i++)good=good&&((r[i]>='0'&&r[i]<='9')||(r[i]>='a'&&r[i]<='f'));
            CHECK(good);
        }
    }
    CHECK(access(at(root,INDEX_JSON ".tmp"),F_OK)!=0);
    CHECK(xz_dj_stick_set_active(root,"djp_kai")==1);
    size_t k2;char *same=slurp(at(root,INDEX_JSON),&k2);CHECK(same&&k2==k&&!memcmp(same,after,k));free(same);
    CHECK(xz_dj_stick_set_active(root,"nobody")==-1);
    CHECK(xz_dj_stick_set_active(root,"../x")==-1);
    CHECK(xz_dj_stick_set_active(root,"")==-1);
    same=slurp(at(root,INDEX_JSON),&k2);CHECK(same&&k2==k&&!memcmp(same,after,k));free(same);
    CHECK(xz_dj_stick_set_active(root,"djp_lena")==0);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.active==1);
    free(before);free(after);

    /* A tile with the wrong bytes: monogram, not damaged. */
    char *tile=slurp(at(root,KAI_TILE),&n);CHECK(tile!=NULL);
    if(tile){
        tile[100]^=1;CHECK(spit(at(root,KAI_TILE),tile,n)==0);
        CHECK(xz_dj_stick_read(root,&s)==0);CHECK(!s.profile[0].has_thumb);CHECK(!s.profile[0].damaged);
        CHECK(!strcmp(s.profile[0].name,"DJ Kai"));CHECK(!strcmp(s.profile[0].tile_path,""));CHECK(!strcmp(s.profile[0].document_path,KAI_JSON));CHECK(s.profile[1].has_thumb);
        tile[100]^=1;CHECK(spit(at(root,KAI_TILE),tile,n)==0);free(tile);
    }
    CHECK(xz_dj_stick_read(root,&s)==0);check_fixture(&s);

    /* Damaged: unparseable, then a valid document under the wrong folder. */
    char *kai=slurp(at(root,KAI_JSON),&n),*lena=slurp(at(root,LENA_JSON),&k);CHECK(kai&&lena);
    if(kai&&lena){
        CHECK(spit(at(root,KAI_JSON),"{",1)==0);
        CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==2);CHECK(s.profile[0].damaged);
        CHECK(!strcmp(s.profile[0].name,"DJ Kai"));CHECK(!strcmp(s.profile[0].dj_id,"djp_kai"));CHECK(!s.profile[0].has_thumb);
        CHECK(!strcmp(s.profile[0].document_path,""));CHECK(!strcmp(s.profile[0].tile_path,""));
        CHECK(!s.profile[1].damaged);CHECK(s.profile[1].has_thumb);CHECK(!strcmp(s.profile[1].name,"Lena B"));
        CHECK(spit(at(root,KAI_JSON),lena,k)==0);
        CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.profile[0].damaged);CHECK(!strcmp(s.profile[0].name,"DJ Kai"));
        CHECK(unlink(at(root,KAI_JSON))==0);
        CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.profile[0].damaged);CHECK(s.active==1);
        CHECK(spit(at(root,KAI_JSON),kai,n)==0);
    }
    free(kai);free(lena);
    CHECK(xz_dj_stick_read(root,&s)==0);check_fixture(&s);

    /* No index and no legacy card: no profile. An invalid index falls back the same way. */
    char *index=slurp(at(root,INDEX_JSON),&n);CHECK(index!=NULL);
    CHECK(spit(at(root,INDEX_JSON),"{\"schema\":\"x\"}",14)==0);
    CHECK(xz_dj_stick_read(root,&s)==1);CHECK(s.count==0);CHECK(s.active==-1);
    CHECK(xz_dj_stick_set_active(root,"djp_kai")==-1);
    CHECK(unlink(at(root,INDEX_JSON))==0);
    CHECK(xz_dj_stick_read(root,&s)==1);CHECK(s.count==0);CHECK(s.active==-1);
    CHECK(xz_dj_stick_set_active(root,"djp_kai")==-1);
    CHECK(access(at(root,INDEX_JSON),F_OK)!=0);

    /* Legacy card. */
    CHECK(mkdir(at(root,"VJ.Tools"),0755)==0);CHECK(mkdir(at(root,"VJ.Tools/Branding"),0755)==0);
    static const char legacy[]="{\"schema\":\"vj.tools.dj-profile\",\"version\":1,\"dj_id\":\"djp_lena\","
        "\"revision\":\"5e1a9d40-0000-4000-8000-0000000b2b00\",\"dj_name\":\"Lena B\",\"note\":\"\",\"bundle\":\"\",\"secondary_logos\":[]}";
    CHECK(xz_dj_profile_valid(legacy,sizeof(legacy)-1)==0);
    CHECK(spit(at(root,"VJ.Tools/Branding/profile.json"),legacy,sizeof(legacy)-1)==0);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==1);CHECK(s.legacy==1);CHECK(s.active==0);
    CHECK(!strcmp(s.profile[0].name,"Lena B"));CHECK(!s.profile[0].has_thumb);CHECK(!s.profile[0].damaged);
    CHECK(!strcmp(s.profile[0].document_path,"VJ.Tools/Branding/profile.json"));
    CHECK(xz_dj_stick_set_active(root,"djp_lena")==-1);

    /* Names and taglines are copied whole: 160 two-byte characters; an over-long page_url is dropped. */
    char wide[2048];int w=snprintf(wide,sizeof(wide),"{\"schema\":\"vj.tools.dj-profile\",\"version\":1,\"dj_id\":\"w\",\"revision\":\"r\",\"dj_name\":\"");
    for(int i=0;i<160;i++){wide[w++]=(char)0xc3;wide[w++]=(char)0xa9;}
    w+=snprintf(wide+w,sizeof(wide)-(size_t)w,"\",\"tagline\":\"x\\u00e9\",\"page_url\":\"https://e.example/");
    for(int i=0;i<600;i++)wide[w++]='a';
    w+=snprintf(wide+w,sizeof(wide)-(size_t)w,"\"}");
    CHECK(xz_dj_profile_valid(wide,(size_t)w)==0);
    CHECK(spit(at(root,"VJ.Tools/Branding/profile.json"),wide,(size_t)w)==0);
    CHECK(xz_dj_stick_read(root,&s)==0);
    CHECK(strlen(s.profile[0].name)==320);CHECK(!strcmp(s.profile[0].tagline,"x\xc3\xa9"));CHECK(!strcmp(s.profile[0].page_url,""));

    /* Index back, a symlinked profile is not followed. */
    if(index){
        CHECK(spit(at(root,INDEX_JSON),index,n)==0);
        CHECK(xz_dj_stick_read(root,&s)==0);check_fixture(&s);
        CHECK(rename(at(root,KAI_JSON),at(root,"kai.json"))==0);
        CHECK(symlink(at(root,"kai.json"),at(root,KAI_JSON))==0);
        CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.profile[0].damaged);
        CHECK(unlink(at(root,KAI_JSON))==0);CHECK(rename(at(root,"kai.json"),at(root,KAI_JSON))==0);
        CHECK(xz_dj_stick_read(root,&s)==0);check_fixture(&s);
    }
    free(index);

    size_t n2;char *music2=slurp(at(root,MUSIC),&n2);
    CHECK(music&&music2&&n2==strlen(music)&&!memcmp(music,music2,n2));
    free(music);free(music2);
    printf("stick: %s (%d failures)\n",failures?"FAIL":"ok",failures);
    return failures?1:0;
}

/* serde_json edge cases the shared fixtures do not reach. */
#define IDX_HEAD "{\"schema\":\"vjtools.dj-profile-index\",\"version\":1,\"revision\":\"r1\",\"active\":\"a\","
#define IDX(entry,tail) IDX_HEAD "\"profiles\":[{\"dj_id\":\"a\",\"dj_name\":" entry "}]" tail "}"
#define PRO_HEAD "{\"schema\":\"vj.tools.dj-profile\",\"version\":1,\"dj_id\":\"x\",\"revision\":\"r\",\"dj_name\":"
#define PRO(name,tail) PRO_HEAD name tail "}"
static int syntax(void){
    static const struct { int index, valid; const char *json; } cases[]={
        {1,1,IDX("\"A\"","")},
        {1,1,IDX("\"A\"","")"  \n"},
        {1,0,IDX("\"A\"","")"x"},
        {1,0,"\xef\xbb\xbf" IDX("\"A\"","")},
        {1,0,IDX("\"A\"",",")},
        {1,0,IDX("\"A\"",",\"active\":\"a\"")},
        {1,1,IDX("\"A\"",",\"future\":{\"k\":[1,2.5e3,true,null,\"\\ud800\"]}")},
        {1,1,IDX("\"A\"",",\"f\":{\"\\ud800\":1}")},
        {1,0,IDX("\"A\"",",\"\\ud800\":1")},
        {1,1,IDX("\"A\"",",\"f\":\"\xff\"")},
        {1,0,IDX("\"\xff\"","")},
        {1,0,IDX("\"\\ud800\"","")},
        {1,0,IDX("\"\\udc00\"","")},
        {1,1,IDX("\"\\ud83d\\ude00\"","")},
        {1,0,IDX("\"\\u00a0\"","")},
        {1,1,IDX("\"\\u200b\"","")},
        {1,0,IDX("\"\\u0085x\"","")},
        {1,0,IDX("\"   \"","")},
        {1,0,IDX("\"A\\u0000\"","")},
        {1,0,IDX("\"A\"",",\"x\":01")},
        {1,0,IDX("\"A\"",",\"x\":1.")},
        {1,0,IDX("\"A\"",",\"x\":\"\\q\"")},
        {1,0,IDX("\"A\"",",\"x\":\"\t\"")},
        {1,1,IDX("\"A\"",",\"x\\u0000\":1")},
        {1,0,"[]"},
        {1,0,""},
        {1,0,"{\"schema\":\"vjtools.dj-profile-index\",\"version\":1.0,\"revision\":\"r1\",\"active\":\"a\",\"profiles\":[{\"dj_id\":\"a\",\"dj_name\":\"A\"}]}"},
        {1,0,"{\"schema\":\"vjtools.dj-profile-index\",\"version\":-0,\"revision\":\"r1\",\"active\":\"a\",\"profiles\":[{\"dj_id\":\"a\",\"dj_name\":\"A\"}]}"},
        {1,0,"{\"schema\":\"vjtools.dj-profile-index\",\"version\":1,\"revision\":\"r1\",\"active\":null,\"profiles\":[{\"dj_id\":\"a\",\"dj_name\":\"A\"}]}"},
        {1,0,"{\"schema\":\"vjtools.dj-profile-index\",\"version\":1,\"revision\":\"r1\",\"active\":\"a\",\"profiles\":null}"},
        {1,1,"{\"sch\\u0065ma\":\"vjtools.dj-profile-index\",\"version\":1,\"revision\":\"r1\",\"active\":\"a\",\"profiles\":[{\"dj_id\":\"a\",\"dj_name\":\"A\"}]}"},
        {0,1,PRO("\"X\"","")},
        {0,0,PRO("\"X\"",",\"note\":null")},
        {0,1,PRO("\"X\"",",\"accent\":null,\"page_url\":null,\"preferred_logo\":null,\"xz_tile\":null")},
        {0,0,PRO("\"X\"",",\"aliases\":null")},
        {0,0,PRO("\"X\"",",\"secondary_logos\":null")},
        {0,1,PRO("\"X\"",",\"accent\":\"#FFaa00\"")},
        {0,0,PRO("\"X\"",",\"accent\":\"#FFaa0\"")},
        {0,1,PRO("\"X\"",",\"page_url\":\"https://a\"")},
        {0,0,PRO("\"X\"",",\"page_url\":\"https://\"")},
        {0,0,PRO("\"X\"",",\"page_url\":\"https:///x\"")},
        {0,0,PRO("\"X\"",",\"page_url\":\"https://a b\"")},
        {0,0,PRO("\"X\"",",\"page_url\":\"https://a\\u3000b\"")},
        {0,0,PRO("\"X\"",",\"page_url\":\"HTTPS://a\"")},
        {0,1,PRO("\"X\"",",\"tagline\":\"\"")},
        {0,0,PRO("\"X\"",",\"tagline\":\"a\\nb\"")},
        {0,1,PRO("\"X\"",",\"note\":\"a\\nb\\tc\"")},
        {0,0,PRO("\"X\"",",\"note\":\"a\\u0007\"")},
        {0,0,PRO("\"X\"",",\"links\":[{\"kind\":\" \",\"url\":\"https://a\"}]")},
        {0,0,PRO("\"X\"",",\"links\":[{\"url\":\"https://a\"}]")},
        {0,1,PRO("\"X\"",",\"links\":[{\"kind\":\"w\",\"url\":\"https://a\",\"extra\":1}]")},
        {0,0,PRO("\"X\"",",\"xz_tile\":{\"path\":\"VJ.Tools/Branding/bundles/b/renditions/t\",\"bytes\":51200,\"sha256\":\""
                 "0000000000000000000000000000000000000000000000000000000000000000\",\"width\":160,\"height\":160,\"format\":\"rgb565le\"}")},
        {0,1,PRO("\"X\"",",\"bundle\":\"VJ.Tools/Branding/bundles/b\",\"xz_tile\":{\"path\":\"VJ.Tools/Branding/bundles/b/renditions/t\",\"bytes\":51200,\"sha256\":\""
                 "0000000000000000000000000000000000000000000000000000000000000000\",\"width\":160,\"height\":160,\"format\":\"rgb565le\"}")},
        {0,0,PRO("\"X\"",",\"bundle\":\"VJ.Tools/Branding/bundles/b\",\"xz_tile\":{\"path\":\"VJ.Tools/Branding/bundles/b/renditions/t\",\"bytes\":51200,\"sha256\":\""
                 "0000000000000000000000000000000000000000000000000000000000000000\",\"width\":160,\"height\":160}")},
        {0,0,PRO("\"X\"",",\"bundle\":\"VJ.Tools/Branding/bundles/b.\"")},
        {0,0,PRO("\"X\"",",\"bundle\":\"VJ.Tools/Branding/bundles/b\",\"secondary_logos\":[{\"path\":\"VJ.Tools/Branding/bundles/b/assets/a.png\",\"bytes\":1,\"sha256\":\""
                 "0000000000000000000000000000000000000000000000000000000000000000\"}]")},
        {0,1,PRO("\"X\"",",\"preferred_logo\":{\"path\":\"VJ.Tools/Branding/bundles/b/assets/a.png\",\"bytes\":1,\"sha256\":\""
                 "0000000000000000000000000000000000000000000000000000000000000000\"}")},
        {0,0,PRO("\"X\"",",\"preferred_logo\":{\"path\":\"VJ.Tools/Branding/bundles/b/assets/a.png\",\"bytes\":0,\"sha256\":\""
                 "0000000000000000000000000000000000000000000000000000000000000000\"}")},
        {0,0,PRO("\"X\"",",\"preferred_logo\":{\"path\":\"VJ.Tools/Branding/bundles/b/assets/a.png\",\"bytes\":18446744073709551616,\"sha256\":\""
                 "0000000000000000000000000000000000000000000000000000000000000000\"}")},
        {0,0,PRO("\"X\"",",\"preferred_logo\":{\"path\":\"VJ.Tools/Branding/bundles/b/assets/a?.png\",\"bytes\":1,\"sha256\":\""
                 "0000000000000000000000000000000000000000000000000000000000000000\"}")},
        {0,0,PRO("\"X\"",",\"dj_name\":\"Y\"")},
        {0,0,"{\"schema\":\"vj.tools.dj-profile\",\"version\":1,\"dj_id\":\"x\",\"revision\":\"r\"}"},
        {0,0,"{\"schema\":\"vj.tools.dj-profile\",\"version\":2,\"dj_id\":\"x\",\"revision\":\"r\",\"dj_name\":\"X\"}"},
        {0,0,"{\"schema\":\"vj.tools.dj-profile\",\"version\":4294967297,\"dj_id\":\"x\",\"revision\":\"r\",\"dj_name\":\"X\"}"},
    };
    int bad=0,total=(int)(sizeof(cases)/sizeof(cases[0]));
    for(int i=0;i<total;i++){
        size_t n=strlen(cases[i].json);
        int got=(cases[i].index?xz_dj_index_valid(cases[i].json,n):xz_dj_profile_valid(cases[i].json,n))==0;
        if(got!=cases[i].valid){fprintf(stderr,"syntax case %d (%s) expected %s: %s\n",i,cases[i].index?"index":"profile",
            cases[i].valid?"valid":"invalid",cases[i].json);bad++;}
    }
    /* Tagline: 80 two-byte characters pass, 81 fail; the 64 KiB ceiling is inclusive. */
    char text[70000];
    for(int count=80;count<=81;count++){
        int w=snprintf(text,sizeof(text),PRO_HEAD "\"X\",\"tagline\":\"");
        for(int i=0;i<count;i++){text[w++]=(char)0xc3;text[w++]=(char)0xa9;}
        w+=snprintf(text+w,sizeof(text)-(size_t)w,"\"}");
        if((xz_dj_profile_valid(text,(size_t)w)==0)!=(count==80)){fprintf(stderr,"tagline %d chars wrong\n",count);bad++;}
    }
    int w=snprintf(text,sizeof(text),PRO_HEAD "\"X\",\"pad\":\"");
    int pad=65536-w-2;memset(text+w,'a',(size_t)pad);w+=pad;w+=snprintf(text+w,sizeof(text)-(size_t)w,"\"}");
    if(xz_dj_profile_valid(text,(size_t)w)!=0||w!=65536){fprintf(stderr,"64 KiB document rejected\n");bad++;}
    text[w-2]='a';text[w-1]='"';text[w]='}';
    if(xz_dj_profile_valid(text,(size_t)w+1)==0){fprintf(stderr,"64 KiB + 1 document accepted\n");bad++;}
    /* Deep nesting inside an unknown field: serde_json's ignore path has no depth limit. */
    w=snprintf(text,sizeof(text),PRO_HEAD "\"X\",\"deep\":");
    for(int i=0;i<1000;i++)text[w++]='[';
    for(int i=0;i<1000;i++)text[w++]=']';
    text[w++]='}';
    if(xz_dj_profile_valid(text,(size_t)w)!=0){fprintf(stderr,"deep unknown field rejected\n");bad++;}
    printf("syntax: %d/%d ok\n",total+4-bad,total+4);
    return bad?1:0;
}

int main(int argc,char **argv){
    if(argc==4&&!strcmp(argv[1],"case")){
        size_t n;char *json=slurp(argv[3],&n);if(!json){fprintf(stderr,"cannot read %s\n",argv[3]);return 2;}
        int rc=!strcmp(argv[2],"index")?xz_dj_index_valid(json,n):!strcmp(argv[2],"profile")?xz_dj_profile_valid(json,n):-2;
        free(json);if(rc==-2)return 2;
        puts(rc?"invalid":"valid");return 0;
    }
    if(argc==3&&!strcmp(argv[1],"stick"))return stick(argv[2]);
    if(argc==2&&!strcmp(argv[1],"syntax"))return syntax();
    fprintf(stderr,"usage: %s case index|profile <file> | stick <root> | syntax\n",argv[0]);return 2;
}
