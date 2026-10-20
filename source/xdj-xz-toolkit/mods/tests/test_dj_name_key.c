/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "dj_name_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* test_dj_name_key          built-in checks
   test_dj_name_key lines    reads names one per line from stdin, prints each key on its own line */
static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#x);failures++;}}while(0)

static int key_is(const char *in,const char *want){
    char out[256];int n=xz_dj_name_key(in,strlen(in),out,sizeof out);
    return n==(int)strlen(want)&&!memcmp(out,want,(size_t)n+1);
}
static int lines(void){
    char *line=NULL,*out=NULL;size_t size=0,cap=0;ssize_t n;
    while((n=getline(&line,&size,stdin))>=0){
        if(n&&line[n-1]=='\n')n--;
        if(cap<XZ_DJ_NAME_KEY_CAP((size_t)n)){cap=XZ_DJ_NAME_KEY_CAP((size_t)n);free(out);if(!(out=malloc(cap)))return 1;}
        int k=xz_dj_name_key(line,(size_t)n,out,cap);
        if(k<0){fprintf(stderr,"key failed for a %zd byte line\n",n);return 1;}
        fwrite(out,1,(size_t)k,stdout);putchar('\n');
    }
    free(line);free(out);return 0;
}
int main(int argc,char **argv){
    if(argc>1&&!strcmp(argv[1],"lines"))return lines();
    CHECK(key_is("Beyonce\xcc\x81","beyonc\xc3\xa9"));
    CHECK(key_is("Beyonc\xc3\xa9","beyonc\xc3\xa9"));
    CHECK(key_is("DJ Kai","dj kai"));
    CHECK(!xz_dj_name_key_compare("DJ Kai","dj kai"));
    CHECK(!xz_dj_name_key_compare("Beyonce\xcc\x81","BEYONC\xc3\xa9"));
    CHECK(key_is("E\xcc\x81","\xc3\x89"));                                    /* folding is ASCII only, after NFC */
    CHECK(key_is("\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab","\xed\x95\x9c"));     /* jamo HIEUH A NIEUN -> U+D55C */
    CHECK(key_is("\xed\x95\x9c","\xed\x95\x9c"));
    CHECK(key_is("\xe3\x81\x8b\xe3\x82\x99","\xe3\x81\x8c"));                 /* U+304B U+3099 -> U+304C */
    CHECK(key_is("\xe2\x84\xab","\xc3\x85"));                                 /* U+212B -> U+00C5, not folded */
    CHECK(key_is("c\xcc\x81\xcc\xa7","\xe1\xb8\x89"));                        /* reorder 0327 before 0301 -> U+1E09 */
    CHECK(key_is("\xe0\xa5\x98","\xe0\xa4\x95\xe0\xa4\xbc"));                 /* excluded U+0958 stays decomposed */
    CHECK(key_is("\xf0\x9d\x85\xa0","\xf0\x9d\x85\x98\xf0\x9d\x85\xa5\xf0\x9d\x85\xae")); /* U+1D160, 4 bytes -> 12 */
    CHECK(key_is("A\xff\xc3" "B","a\xff\xc3" "b"));
    CHECK(key_is("e\xff\xcc\x81","e\xff\xcc\x81"));                           /* an invalid byte blocks composition */
    CHECK(key_is("\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80","\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80"));
    CHECK(key_is("\xcc\x81" "e","\xcc\x81" "e"));
    char small[8];
    CHECK(xz_dj_name_key("abc",3,small,3)==-1);
    CHECK(xz_dj_name_key("abc",3,small,4)==3&&!strcmp(small,"abc"));
    CHECK(xz_dj_name_key("e\xcc\x81",3,small,2)==-1);
    CHECK(xz_dj_name_key("e\xcc\x81",3,small,3)==2&&!strcmp(small,"\xc3\xa9"));
    CHECK(xz_dj_name_key("\xf0\x9d\x85\xa0",4,small,12)==-1);
    CHECK(xz_dj_name_key("A\0B",3,small,4)==3&&!memcmp(small,"a\0b",4));
    CHECK(xz_dj_name_key_compare("B","a")>0&&xz_dj_name_key_compare("a","B")<0);
    CHECK(xz_dj_name_key_compare("\xc3\x89","e")>0);
    CHECK(xz_dj_name_key_compare("ab","abc")<0);
    char a[4097],b[4098];
    memset(a,'A',4094);memcpy(a+4094,"\xc3\xa9",3);
    memset(b,'a',4094);memcpy(b+4094,"e\xcc\x81",4);
    CHECK(strlen(a)==4096&&strlen(b)==4097);
    CHECK(xz_dj_name_key_compare(a,b)==0);
    b[4094]='F';
    CHECK(xz_dj_name_key_compare(a,b)>0);
    size_t marks=8192;char *run=malloc(1+2*marks),*out=malloc(XZ_DJ_NAME_KEY_CAP(1+2*marks));
    CHECK(run&&out);
    if(run&&out){
        run[0]='a';
        for(size_t i=0;i<marks;i++)memcpy(run+1+2*i,i%2?"\xcc\xa7":"\xcc\x81",2);
        int n=xz_dj_name_key(run,1+2*marks,out,XZ_DJ_NAME_KEY_CAP(1+2*marks));
        CHECK(n==(int)(2*marks));
        CHECK(n>0&&!memcmp(out,"\xc3\xa1",2)&&!memcmp(out+2,"\xcc\xa7",2)&&!memcmp(out+2*marks/2,"\xcc\xa7\xcc\x81",4));
    }
    free(run);free(out);
    printf("test_dj_name_key: %s\n",failures?"FAIL":"ok");
    return failures!=0;
}
