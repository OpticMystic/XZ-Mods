/* SPDX-License-Identifier: MIT */
#include "shell_library.h"
#include "pdb/pdb.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct node { uint32_t id, parent, sort, name; int folder; };
struct xz_shlib {
 FILE *file; uint64_t size;
 void *arena; pdb *db; size_t mark;
 struct xz_shlib_track *tracks; uint32_t count, cap;
 char *text; size_t text_used, text_cap;
 struct node *playlists, *history; uint32_t playlist_count, playlist_cap, history_count, history_cap;
 int failed;
};

static int read_file(void *user, uint64_t offset, void *bytes, size_t length) {
 FILE *f=user;
 if(offset>(uint64_t)LONG_MAX||fseek(f,(long)offset,SEEK_SET))return -1;
 return fread(bytes,1,length,f)==length?0:-1;
}
static int grow(void **p, uint32_t *cap, uint32_t need, size_t size) {
 if(need<=*cap)return 0;
 uint32_t next=*cap?*cap:64;
 while(next<need){if(next>UINT32_MAX/2)return -1;next*=2;}
 void *q=realloc(*p,(size_t)next*size);if(!q)return -1;
 *p=q;*cap=next;return 0;
}
/* Text pool: offset 0 is the empty string. */
static uint32_t text_add(struct xz_shlib *L, const char *s, size_t n) {
 if(!n)return 0;
 if(L->text_used+n+1>L->text_cap){
  size_t next=L->text_cap?L->text_cap:65536;
  while(next<L->text_used+n+1){if(next>(size_t)UINT32_MAX/2){L->failed=1;return 0;}next*=2;}
  char *q=realloc(L->text,next);if(!q){L->failed=1;return 0;}
  L->text=q;L->text_cap=next;
 }
 uint32_t at=(uint32_t)L->text_used;
 memcpy(L->text+at,s,n);L->text[at+n]=0;L->text_used+=n+1;
 return at;
}
static uint32_t text_of(struct xz_shlib *L, pdb_text t) { return text_add(L,t.data,t.data?t.len:0); }
static int on_track(void *user, const pdb_track *t) {
 struct xz_shlib *L=user;
 if(grow((void **)&L->tracks,&L->cap,L->count+1,sizeof *L->tracks)){L->failed=1;return 1;}
 struct xz_shlib_track *r=&L->tracks[L->count++];
 memset(r,0,sizeof *r);
 r->id=t->id;r->artist_id=t->artist_id;r->album_id=t->album_id;r->genre_id=t->genre_id;r->key_id=t->key_id;
 r->bpm_x100=t->bpm_x100;r->duration_ms=(uint32_t)t->duration_seconds*1000u;r->bitrate=t->bitrate;
 r->rating=t->rating>5?5:t->rating;r->color_id=t->color_id<=8?t->color_id:0;
 r->title=text_of(L,t->title);r->artist=text_of(L,t->artist);r->album=text_of(L,t->album);
 r->genre=text_of(L,t->genre);r->label=text_of(L,t->label);r->key=text_of(L,t->key);
 r->date_added=text_of(L,t->date_added);
 const char *path=t->file_path.data;size_t n=path?t->file_path.len:0,base=0;
 for(size_t i=0;i<n;i++)if(path[i]=='/')base=i+1;
 r->file_name=text_add(L,path?path+base:NULL,n-base);
 r->file_path=text_add(L,path,n);
 return L->failed;
}
static int add_node(struct xz_shlib *L, struct node **nodes, uint32_t *count, uint32_t *cap, const pdb_playlist *p) {
 if(grow((void **)nodes,cap,*count+1,sizeof **nodes)){L->failed=1;return 1;}
 (*nodes)[(*count)++]=(struct node){p->id,p->parent_id,p->sort_order,text_of(L,p->name),p->is_folder};
 return L->failed;
}
static int on_playlist(void *user, const pdb_playlist *p) { struct xz_shlib *L=user;return add_node(L,&L->playlists,&L->playlist_count,&L->playlist_cap,p); }
static int on_history(void *user, const pdb_playlist *p) { struct xz_shlib *L=user;return add_node(L,&L->history,&L->history_count,&L->history_cap,p); }
static int by_id(const void *a, const void *b) {
 uint32_t x=((const struct xz_shlib_track *)a)->id,y=((const struct xz_shlib_track *)b)->id;
 return x<y?-1:x>y;
}
int xz_shlib_open(const char *path, size_t arena_size, struct xz_shlib **out) {
 if(!out)return -1;
 *out=NULL;
 struct xz_shlib *L=calloc(1,sizeof *L);
 if(!L)return -1;
 L->file=fopen(path,"rb");
 long size=-1;
 if(L->file&&!fseek(L->file,0,SEEK_END))size=ftell(L->file);
 L->arena=malloc(arena_size);
 if(size<=0||!L->arena||text_add(L,"",0)!=0||
    pdb_open(&L->db,read_file,L->file,(uint64_t)size,L->arena,arena_size)!=PDB_OK){xz_shlib_free(L);return -1;}
 L->size=(uint64_t)size;
 /* Missing playlist or history tables are empty lists, not failures. */
 int rc=pdb_each_track(L->db,on_track,L);
 if(rc==PDB_OK)rc=pdb_each_playlist(L->db,on_playlist,L);
 if(rc==PDB_OK)rc=pdb_each_history(L->db,on_history,L);
 if(rc!=PDB_OK||L->failed){xz_shlib_free(L);return -1;}
 if(L->count)qsort(L->tracks,L->count,sizeof *L->tracks,by_id);
 L->mark=pdb_mark(L->db);
 *out=L;return 0;
}
void xz_shlib_free(struct xz_shlib *L) {
 if(!L)return;
 if(L->file)fclose(L->file);
 free(L->arena);free(L->tracks);free(L->text);free(L->playlists);free(L->history);free(L);
}
uint32_t xz_shlib_track_count(const struct xz_shlib *L) { return L?L->count:0; }
const struct xz_shlib_track *xz_shlib_track(const struct xz_shlib *L, uint32_t id) {
 if(!L)return NULL;
 uint32_t lo=0,hi=L->count;
 while(lo<hi){
  uint32_t mid=lo+(hi-lo)/2;
  if(L->tracks[mid].id==id)return &L->tracks[mid];
  if(L->tracks[mid].id<id)lo=mid+1;else hi=mid;
 }
 return NULL;
}
const char *xz_shlib_text(const struct xz_shlib *L, uint32_t offset) { return L&&offset<L->text_used?L->text+offset:""; }

/* ---- lists ---- */
static const char *const category_names[XZ_SHELL_CAT_COUNT]={
 "ARTIST","ALBUM","TRACK","KEY","BPM","GENRE","RATING","COLOR","PLAYLIST","FOLDER","HISTORY","MATCHING"
};
uint32_t xz_shlib_categories(void) {
 return (1u<<XZ_SHELL_CAT_COUNT)-1u&~(1u<<XZ_SHELL_CAT_FOLDER)&~(1u<<XZ_SHELL_CAT_MATCHING);
}
static uint32_t name_add(struct xz_shlib_list *l, const char *s, size_t n) {
 if(l->names_used+n+1>l->names_cap){
  size_t next=l->names_cap?l->names_cap:4096;
  while(next<l->names_used+n+1)next*=2;
  char *q=realloc(l->names,next);if(!q)return UINT32_MAX;
  l->names=q;l->names_cap=next;
 }
 uint32_t at=(uint32_t)l->names_used;
 if(n)memcpy(l->names+at,s,n);
 l->names[at+n]=0;l->names_used+=n+1;
 return at;
}
static int row_add(struct xz_shlib_list *l, uint32_t *cap, enum xz_shell_browse_row_kind kind, uint32_t id, const char *name, size_t n) {
 if((uint32_t)l->total>=*cap){
  uint32_t next=*cap?*cap*2:256;
  uint8_t *k=realloc(l->kind,next);if(!k)return -1;l->kind=k;
  uint32_t *i=realloc(l->id,(size_t)next*4);if(!i)return -1;l->id=i;
  uint32_t *m=realloc(l->name,(size_t)next*4);if(!m)return -1;l->name=m;
  *cap=next;
 }
 uint32_t at=0;
 if(kind==XZ_SHELL_ROW_FOLDER&&(at=name_add(l,name,n))==UINT32_MAX)return -1;
 l->kind[l->total]=(uint8_t)kind;l->id[l->total]=id;l->name[l->total]=at;l->total++;
 return 0;
}
static uint32_t list_id(const struct xz_shlib_request *r) {
 uint32_t h=2166136261u;
 uint32_t parts[3+XZ_SHLIB_PATH_DEPTH]={(uint32_t)r->screen,(uint32_t)r->category,(uint32_t)r->depth};
 for(int i=0;i<r->depth&&i<XZ_SHLIB_PATH_DEPTH;i++)parts[3+i]=r->path[i];
 for(int i=0;i<3+r->depth&&i<3+XZ_SHLIB_PATH_DEPTH;i++)for(int b=0;b<4;b++){h^=(parts[i]>>(8*b))&255u;h*=16777619u;}
 return h?h:1u;
}
static const struct node *find_node(const struct node *n, uint32_t count, uint32_t id) {
 for(uint32_t i=0;i<count;i++)if(n[i].id==id)return &n[i];
 return NULL;
}
/* The group's own name, or a label for numeric and empty groups formatted into buf. */
static pdb_text group_name(char *buf, size_t size, enum xz_shell_browse_category c, uint32_t group, pdb_text name) {
 if(name.data&&name.len)return name;
 if(c==XZ_SHELL_CAT_BPM)snprintf(buf,size,"%u.%02u",group/100u,group%100u);
 else if(c==XZ_SHELL_CAT_RATING){size_t n=group>5?5:group;memset(buf,'*',n);buf[n]=0;if(!n)snprintf(buf,size,"NO RATING");}
 else snprintf(buf,size,c==XZ_SHELL_CAT_COLOR?"NO COLOR":"(UNKNOWN)");
 return (pdb_text){buf,strlen(buf)};
}
static int build_index(struct xz_shlib *L, const struct xz_shlib_request *r, struct xz_shlib_list *l, uint32_t *cap) {
 static const pdb_category map[XZ_SHELL_CAT_COUNT]={
  [XZ_SHELL_CAT_ARTIST]=PDB_ARTIST,[XZ_SHELL_CAT_ALBUM]=PDB_ALBUM,[XZ_SHELL_CAT_TRACK]=PDB_TRACK,[XZ_SHELL_CAT_KEY]=PDB_KEY,
  [XZ_SHELL_CAT_BPM]=PDB_BPM,[XZ_SHELL_CAT_GENRE]=PDB_GENRE,[XZ_SHELL_CAT_RATING]=PDB_RATING,[XZ_SHELL_CAT_COLOR]=PDB_COLOR};
 pdb_index index;
 if(pdb_build_index(L->db,map[r->category],&index)!=PDB_OK)return -1;
 char buf[32];pdb_text name;
 if(r->category==XZ_SHELL_CAT_TRACK){
  if(r->depth)return -1;
  for(size_t i=0;i<index.count;i++)if(row_add(l,cap,XZ_SHELL_ROW_TRACK,index.entries[i].track_id,NULL,0))return -1;
  return 0;
 }
 if(r->depth==0){
  l->folders=1;
  for(size_t i=0;i<index.count;i++){
   const pdb_index_entry *e=&index.entries[i];
   if(i&&e->group_id==index.entries[i-1].group_id)continue;
   name=group_name(buf,sizeof buf,r->category,e->group_id,e->name);
   if(row_add(l,cap,XZ_SHELL_ROW_FOLDER,e->group_id,name.data,name.len))return -1;
  }
  return 0;
 }
 if(r->depth!=1)return -1;
 int found=0;
 for(size_t i=0;i<index.count;i++){
  const pdb_index_entry *e=&index.entries[i];
  if(e->group_id!=r->path[0])continue;
  if(!found){
   found=1;name=group_name(buf,sizeof buf,r->category,e->group_id,e->name);
   if((l->level_name=name_add(l,name.data,name.len))==UINT32_MAX)return -1;
  }
  if(row_add(l,cap,XZ_SHELL_ROW_TRACK,e->track_id,NULL,0))return -1;
 }
 if(!found&&(l->level_name=name_add(l,"",0))==UINT32_MAX)return -1;
 return 0;
}
static const struct node *sort_nodes_base;
static int by_sort(const void *a, const void *b) {
 const struct node *x=&sort_nodes_base[*(const uint32_t *)a],*y=&sort_nodes_base[*(const uint32_t *)b];
 return x->sort<y->sort?-1:x->sort>y->sort?1:x->id<y->id?-1:x->id>y->id;
}
static int build_tree(struct xz_shlib *L, const struct xz_shlib_request *r, struct xz_shlib_list *l, uint32_t *cap, int history) {
 const struct node *nodes=history?L->history:L->playlists;
 uint32_t count=history?L->history_count:L->playlist_count;
 uint32_t parent=r->depth?r->path[r->depth-1]:0;
 const struct node *open=r->depth?find_node(nodes,count,parent):NULL;
 const struct xz_shpl *edited=!history&&r->depth?xz_shpl_find(r->edits,parent):NULL;
 if(edited&&edited->deleted)return -1;
 if(edited&&edited->created){
  /* A playlist made in the shell: its own name and tracks. */
  if((l->level_name=name_add(l,edited->name,strlen(edited->name)))==UINT32_MAX)return -1;
  for(uint32_t i=0;i<edited->count;i++)if(row_add(l,cap,XZ_SHELL_ROW_TRACK,edited->tracks[i],NULL,0))return -1;
  return 0;
 }
 if(r->depth&&!open)return -1;
 if(open){
  const char *n=edited&&edited->renamed?edited->name:xz_shlib_text(L,open->name);
  if((l->level_name=name_add(l,n,strlen(n)))==UINT32_MAX)return -1;
 }
 if(open&&(!open->folder||history)){
  if(edited&&edited->has_tracks){
   for(uint32_t i=0;i<edited->count;i++)if(row_add(l,cap,XZ_SHELL_ROW_TRACK,edited->tracks[i],NULL,0))return -1;
   return 0;
  }
  const pdb_entry *entries;size_t n;
  if(pdb_playlist_entries(L->db,open->id,history,&entries,&n)!=PDB_OK)return -1;
  for(size_t i=0;i<n;i++)if(row_add(l,cap,XZ_SHELL_ROW_TRACK,entries[i].track_id,NULL,0))return -1;
  return 0;
 }
 /* History playlists are listed flat at the top; the playlist tree by parent. */
 uint32_t *order=malloc((size_t)(count?count:1)*4),n=0;
 if(!order)return -1;
 for(uint32_t i=0;i<count;i++)if(history||nodes[i].parent==parent)order[n++]=i;
 sort_nodes_base=nodes;qsort(order,n,4,by_sort);
 l->folders=1;
 for(uint32_t i=0;i<n;i++){
  const struct xz_shpl *e=history?NULL:xz_shpl_find(r->edits,nodes[order[i]].id);
  if(e&&e->deleted)continue;
  const char *s=e&&e->renamed?e->name:xz_shlib_text(L,nodes[order[i]].name);
  if(row_add(l,cap,XZ_SHELL_ROW_FOLDER,nodes[order[i]].id,s,strlen(s))){free(order);return -1;}
 }
 free(order);
 /* Playlists made in the shell follow the stick's own, in the order they were made. */
 for(uint32_t i=0;!history&&r->edits&&i<r->edits->n;i++){
  const struct xz_shpl *e=&r->edits->p[i];
  if(e->created&&!e->deleted&&e->parent==parent&&row_add(l,cap,XZ_SHELL_ROW_FOLDER,e->id,e->name,strlen(e->name)))return -1;
 }
 return 0;
}

/* Sorting a track list by a column. qsort has no context argument; builds run on one thread. */
static const struct xz_shlib *sort_library;
static const struct xz_shlib_list *sort_list;
static int sort_column, sort_descending;
static int fold_compare(const char *a, const char *b) {
 for(;;a++,b++){
  int x=(unsigned char)*a,y=(unsigned char)*b;
  if(x>='a'&&x<='z')x-=32;
  if(y>='a'&&y<='z')y-=32;
  if(x!=y||!x)return x-y;
 }
}
static int by_column(const void *a, const void *b) {
 uint32_t i=*(const uint32_t *)a,j=*(const uint32_t *)b;
 const struct xz_shlib_track *x=xz_shlib_track(sort_library,sort_list->id[i]),*y=xz_shlib_track(sort_library,sort_list->id[j]);
 int c=0;
 if(x&&y)switch(sort_column){
 case XZ_SHELL_COL_TITLE:c=fold_compare(xz_shlib_text(sort_library,x->title),xz_shlib_text(sort_library,y->title));break;
 case XZ_SHELL_COL_ARTIST:c=fold_compare(xz_shlib_text(sort_library,x->artist),xz_shlib_text(sort_library,y->artist));break;
 case XZ_SHELL_COL_KEY:c=fold_compare(xz_shlib_text(sort_library,x->key),xz_shlib_text(sort_library,y->key));break;
 case XZ_SHELL_COL_BPM:c=x->bpm_x100<y->bpm_x100?-1:x->bpm_x100>y->bpm_x100;break;
 case XZ_SHELL_COL_RATING:c=x->rating<y->rating?-1:x->rating>y->rating;break;
 case XZ_SHELL_COL_COLOR:c=x->color_id<y->color_id?-1:x->color_id>y->color_id;break;
 case XZ_SHELL_COL_TIME:c=x->duration_ms<y->duration_ms?-1:x->duration_ms>y->duration_ms;break;
 case XZ_SHELL_COL_ALBUM:c=fold_compare(xz_shlib_text(sort_library,x->album),xz_shlib_text(sort_library,y->album));break;
 case XZ_SHELL_COL_GENRE:c=fold_compare(xz_shlib_text(sort_library,x->genre),xz_shlib_text(sort_library,y->genre));break;
 case XZ_SHELL_COL_DATE_ADDED:c=strcmp(xz_shlib_text(sort_library,x->date_added),xz_shlib_text(sort_library,y->date_added));break;
 default:break;
 }
 else c=(x!=NULL)-(y!=NULL);
 if(sort_descending)c=-c;
 return c?c:i<j?-1:i>j; /* list position breaks ties, so the sort is stable */
}
static int sort_rows(const struct xz_shlib *L, struct xz_shlib_list *l, int column, int descending) {
 if(l->total<2||(column==XZ_SHELL_COL_NUMBER&&!descending))return 0;
 uint32_t n=(uint32_t)l->total,*order=malloc((size_t)n*4),*ids=malloc((size_t)n*4);
 if(!order||!ids){free(order);free(ids);return -1;}
 for(uint32_t i=0;i<n;i++)order[i]=i;
 if(column==XZ_SHELL_COL_NUMBER){for(uint32_t i=0;i<n;i++)order[i]=n-1-i;}
 else{sort_library=L;sort_list=l;sort_column=column;sort_descending=descending;qsort(order,n,4,by_column);}
 for(uint32_t i=0;i<n;i++)ids[i]=l->id[order[i]];
 memcpy(l->id,ids,(size_t)n*4);
 free(order);free(ids);return 0;
}
static char letter_of(const char *s) {
 char c=s&&*s?*s:'#';
 if(c>='a'&&c<='z')c=(char)(c-32);
 return c>='A'&&c<='Z'?c:'#';
}
static void letters(const struct xz_shlib *L, struct xz_shlib_list *l) {
 char last=0;
 for(int32_t i=0;i<l->total&&l->letter_count<27;i++){
  const char *s;
  if(l->kind[i]==XZ_SHELL_ROW_FOLDER)s=l->names+l->name[i];
  else{const struct xz_shlib_track *t=xz_shlib_track(L,l->id[i]);s=t?xz_shlib_text(L,t->title):"";}
  char c=letter_of(s);
  if(c==last)continue;
  /* Only an alphabetical list gets a jump index. */
  if(last&&c<last&&c!='#'){l->letter_count=0;return;}
  last=c;l->letters[l->letter_count++]=(struct xz_shell_browse_letter){c,i};
 }
}
/* Keeps the track rows the filter accepts, in place. */
static int keep(const struct xz_shlib *L, uint32_t id, const struct xz_shlib_filter *f) {
 const struct xz_shlib_track *t=xz_shlib_track(L,id);
 if(!t)return 0;
 if((f->bpm_min_x100||f->bpm_max_x100)&&((int32_t)t->bpm_x100<f->bpm_min_x100||(f->bpm_max_x100&&(int32_t)t->bpm_x100>f->bpm_max_x100)))return 0;
 if(f->rating_min&&t->rating<f->rating_min)return 0;
 if(f->colors&&(!t->color_id||!((f->colors>>(t->color_id-1u))&1u)))return 0;
 if(f->master_key[0]){
  enum xz_shell_key_match m=xz_shell_key_match(xz_shlib_text(L,t->key),f->master_key);
  if(m!=XZ_SHELL_KEY_MATCH_GREEN&&m!=XZ_SHELL_KEY_MATCH_AMBER)return 0;
 }
 return 1;
}
static void filter_rows(const struct xz_shlib *L, struct xz_shlib_list *l, const struct xz_shlib_filter *f) {
 int32_t n=0;
 for(int32_t i=0;i<l->total;i++)if(l->kind[i]!=XZ_SHELL_ROW_TRACK||keep(L,l->id[i],f)){
  l->kind[n]=l->kind[i];l->id[n]=l->id[i];l->name[n]=l->name[i];n++;
 }
 l->total=n;
}
int xz_shlib_build(struct xz_shlib *L, const struct xz_shlib_request *r, struct xz_shlib_list **out) {
 if(!out)return -1;
 *out=NULL;
 if(!L||!r||r->depth<0||r->depth>XZ_SHLIB_PATH_DEPTH||(unsigned)r->category>=XZ_SHELL_CAT_COUNT)return -1;
 struct xz_shlib_list *l=calloc(1,sizeof *l);
 if(!l)return -1;
 l->screen=r->screen;l->category=r->screen==XZ_SHELL_PLAYLIST?XZ_SHELL_CAT_PLAYLIST:r->category;
 l->depth=r->depth;l->list_id=list_id(r);
 uint32_t cap=0;int rc=-1,ordered=0;
 /* Every build starts from the reader's state right after open. */
 pdb_reset(L->db,L->mark);
 if(r->screen==XZ_SHELL_SEARCH){
  l->category=r->category;
  size_t n=strlen(r->query);const uint32_t *ids;size_t count=0;
  rc=0;
  if(n){
   if(pdb_search(L->db,(pdb_text){r->query,n},&ids,&count)!=PDB_OK)rc=-1;
   for(size_t i=0;!rc&&i<count;i++)if(xz_shlib_track(L,ids[i]))rc=row_add(l,&cap,XZ_SHELL_ROW_TRACK,ids[i],NULL,0);
  }
  if(!rc&&(l->level_name=name_add(l,"SEARCH",6))==UINT32_MAX)rc=-1;
 }else{
  switch(l->category){
  case XZ_SHELL_CAT_PLAYLIST:rc=build_tree(L,r,l,&cap,0);ordered=1;break;
  case XZ_SHELL_CAT_HISTORY:rc=build_tree(L,r,l,&cap,1);ordered=1;break;
  case XZ_SHELL_CAT_FOLDER:case XZ_SHELL_CAT_MATCHING:case XZ_SHELL_CAT_COUNT:rc=-1;break;
  default:rc=build_index(L,r,l,&cap);break;
  }
  const char *top=category_names[l->category];
  if(!rc&&!l->depth&&(l->level_name=name_add(l,top,strlen(top)))==UINT32_MAX)rc=-1;
  if(!rc&&l->depth){
   /* The parent is the category at depth 1, else the folder one level up. */
   const char *parent=top;
   const struct node *p=l->depth>1?find_node(l->category==XZ_SHELL_CAT_HISTORY?L->history:L->playlists,
     l->category==XZ_SHELL_CAT_HISTORY?L->history_count:L->playlist_count,r->path[l->depth-2]):NULL;
   if(p)parent=xz_shlib_text(L,p->name);
   if((l->parent_name=name_add(l,parent,strlen(parent)))==UINT32_MAX)rc=-1;
   else l->has_parent=1;
  }
 }
 pdb_reset(L->db,L->mark);
 if(!rc&&!l->folders&&r->filter.on)filter_rows(L,l,&r->filter);
 /* Default order: playlists and history in their own order, everything else by title (or name). */
 l->sort_column=l->folders?XZ_SHELL_COL_TITLE:ordered||r->screen==XZ_SHELL_SEARCH?XZ_SHELL_COL_NUMBER:XZ_SHELL_COL_TITLE;
 l->sort_descending=0;
 if(!rc&&!l->folders&&r->sort_set&&(unsigned)r->sort_column<XZ_SHELL_COL_COUNT&&
    (r->sort_column!=l->sort_column||r->sort_descending)){
  /* The list's own order descending is its positions reversed. */
  rc=sort_rows(L,l,r->sort_column==l->sort_column?XZ_SHELL_COL_NUMBER:r->sort_column,r->sort_descending);
  l->sort_column=r->sort_column;l->sort_descending=r->sort_descending;
 }
 if(!rc&&l->sort_column==XZ_SHELL_COL_TITLE&&!l->sort_descending)letters(L,l);
 if(rc){xz_shlib_list_free(l);return -1;}
 *out=l;return 0;
}
void xz_shlib_list_free(struct xz_shlib_list *l) {
 if(!l)return;
 free(l->kind);free(l->id);free(l->name);free(l->names);free(l);
}
uint32_t xz_shlib_sort_keys(void) {
 uint32_t k=0;
 static const enum xz_shell_browse_column all[]={XZ_SHELL_COL_NUMBER,XZ_SHELL_COL_TITLE,XZ_SHELL_COL_ARTIST,XZ_SHELL_COL_BPM,
  XZ_SHELL_COL_KEY,XZ_SHELL_COL_RATING,XZ_SHELL_COL_COLOR,XZ_SHELL_COL_TIME,XZ_SHELL_COL_ALBUM,XZ_SHELL_COL_GENRE,XZ_SHELL_COL_DATE_ADDED};
 for(size_t i=0;i<sizeof all/sizeof *all;i++)k|=1u<<all[i];
 return k;
}
int xz_shlib_playlist_tracks(struct xz_shlib *L, uint32_t id, uint32_t **tracks, uint32_t *count) {
 *tracks=NULL;*count=0;
 if(!L)return -1;
 const pdb_entry *entries;size_t n;
 pdb_reset(L->db,L->mark);
 int rc=pdb_playlist_entries(L->db,id,0,&entries,&n)!=PDB_OK?-1:0;
 if(!rc&&n){
  if(!(*tracks=malloc(n*sizeof **tracks)))rc=-1;
  else{for(size_t i=0;i<n;i++)(*tracks)[i]=entries[i].track_id;*count=(uint32_t)n;}
 }
 pdb_reset(L->db,L->mark);
 return rc;
}
const char *xz_shlib_playlist_name(const struct xz_shlib *L, uint32_t id) {
 const struct node *n=L?find_node(L->playlists,L->playlist_count,id):NULL;
 return n?xz_shlib_text(L,n->name):"";
}
int xz_shlib_playlist_refs(struct xz_shlib *L, const struct xz_shpl_set *edits, struct xz_shlib_playlist_ref *out, int capacity) {
 int n=0;
 for(uint32_t i=0;L&&i<L->playlist_count&&n<capacity;i++){
  const struct node *p=&L->playlists[i];
  if(p->folder)continue;
  const struct xz_shpl *e=xz_shpl_find(edits,p->id);
  if(e&&e->deleted)continue;
  out[n].id=p->id;snprintf(out[n].name,sizeof out[n].name,"%s",e&&e->renamed?e->name:xz_shlib_text(L,p->name));
  if(e&&e->has_tracks)out[n].tracks=(int32_t)e->count;
  else{uint32_t *t;uint32_t c;out[n].tracks=xz_shlib_playlist_tracks(L,p->id,&t,&c)?-1:(int32_t)c;free(t);}
  n++;
 }
 for(uint32_t i=0;edits&&i<edits->n&&n<capacity;i++){
  const struct xz_shpl *e=&edits->p[i];
  if(!e->created||e->deleted)continue;
  out[n].id=e->id;snprintf(out[n].name,sizeof out[n].name,"%s",e->name);out[n].tracks=(int32_t)e->count;n++;
 }
 return n;
}
uint32_t xz_shlib_color_rgb(unsigned c) {
 static const uint32_t rgb[9]={0,0xff3c78,0xe62828,0xff8c1a,0xffdc28,0x28e028,0x20c8ff,0x2050ff,0x9b3cff};
 return c<9?rgb[c]:0;
}
#define RF(f) (1u<<XZ_SHELL_RF_##f)
int xz_shlib_window(const struct xz_shlib *L, const struct xz_shlib_list *l, int32_t start, const uint32_t on_deck[2],
                    unsigned on_deck_known, struct xz_shell_browse_row rows[XZ_SHELL_BROWSE_WINDOW]) {
 if(!L||!l||start<0||start>=l->total)return 0;
 int n=0;
 for(int32_t i=start;i<l->total&&n<XZ_SHELL_BROWSE_WINDOW;i++,n++){
  struct xz_shell_browse_row *r=&rows[n];
  memset(r,0,sizeof *r);
  r->kind=(enum xz_shell_browse_row_kind)l->kind[i];r->id=l->id[i];
  if(r->kind==XZ_SHELL_ROW_FOLDER){r->title=l->names+l->name[i];r->known=RF(TITLE);continue;}
  const struct xz_shlib_track *t=xz_shlib_track(L,r->id);
  if(!t)continue;
  r->title=xz_shlib_text(L,t->title);r->artist=xz_shlib_text(L,t->artist);
  r->known=RF(TITLE)|RF(ARTIST)|RF(RATING)|RF(COLOR);
  r->rating=t->rating;r->color=xz_shlib_color_rgb(t->color_id);
  if(t->key){r->key=xz_shlib_text(L,t->key);r->known|=RF(KEY);}
  if(t->bpm_x100&&t->bpm_x100<=INT32_MAX){r->bpm_x100=(int32_t)t->bpm_x100;r->known|=RF(BPM);}
  if(t->duration_ms){r->duration_ms=(int32_t)t->duration_ms;r->known|=RF(DURATION);}
  r->album=xz_shlib_text(L,t->album);r->genre=xz_shlib_text(L,t->genre);r->known|=RF(ALBUM)|RF(GENRE);
  {
   /* The export stores YYYY-MM-DD. */
   const char *d=xz_shlib_text(L,t->date_added);int32_t v=0,digits=0;
   for(;*d&&digits<8;d++)if(*d>='0'&&*d<='9'){v=v*10+(*d-'0');digits++;}
   if(digits==8){r->date_added=v;r->known|=RF(DATE_ADDED);}
  }
  if(on_deck&&(on_deck_known&3u)==3u){
   for(int d=0;d<2;d++)if(on_deck[d]&&on_deck[d]==t->id)r->on_deck|=(uint8_t)(1u<<d);
   r->known|=RF(ON_DECK);
  }
 }
 return n;
}
#define IF(f) XZ_SHELL_KNOWN(XZ_SHELL_I_##f)
int xz_shlib_info(const struct xz_shlib *L, uint32_t id, enum xz_shell_source source, struct xz_shell_info_track *o) {
 const struct xz_shlib_track *t=xz_shlib_track(L,id);
 if(!o)return 0;
 memset(o,0,sizeof *o);
 if(!t||id>INT32_MAX)return 0;
 o->track_id=(int32_t)id;o->known=IF(TRACK_ID)|IF(TITLE)|IF(ARTIST)|IF(ALBUM)|IF(GENRE)|IF(LABEL)|IF(RATING)|IF(COLOR)|IF(SOURCE);
 o->title=xz_shlib_text(L,t->title);o->artist=xz_shlib_text(L,t->artist);o->album=xz_shlib_text(L,t->album);
 o->genre=xz_shlib_text(L,t->genre);o->label=xz_shlib_text(L,t->label);
 o->rating=t->rating;o->color=(enum xz_shell_color_tag)t->color_id;o->source=source;
 if(t->key){o->key=xz_shlib_text(L,t->key);o->known|=IF(KEY);}
 if(t->bpm_x100&&t->bpm_x100<=INT32_MAX){o->bpm_x100=(int32_t)t->bpm_x100;o->known|=IF(BPM);o->links|=IF(BPM);}
 if(t->duration_ms){o->duration_ms=(int32_t)t->duration_ms;o->known|=IF(DURATION);}
 if(t->bitrate&&t->bitrate<=INT32_MAX){o->bitrate_kbps=(int32_t)t->bitrate;o->known|=IF(BITRATE);}
 if(t->date_added){o->date_added=xz_shlib_text(L,t->date_added);o->known|=IF(DATE_ADDED);}
 if(t->file_name){o->file_name=xz_shlib_text(L,t->file_name);o->known|=IF(FILE_NAME);}
 /* Catalog ids for the information jump; 0 means the track has none. */
 if(t->artist_id&&t->artist_id<=INT32_MAX){o->artist_id=(int32_t)t->artist_id;o->links|=IF(ARTIST);}
 if(t->album_id&&t->album_id<=INT32_MAX){o->album_id=(int32_t)t->album_id;o->links|=IF(ALBUM);}
 if(t->genre_id&&t->genre_id<=INT32_MAX){o->genre_id=(int32_t)t->genre_id;o->links|=IF(GENRE);}
 if(t->key_id&&t->key_id<=INT32_MAX){o->key_id=(int32_t)t->key_id;o->links|=IF(KEY);}
 return 1;
}
