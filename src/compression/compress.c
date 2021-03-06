#include <compress.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>



#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(__stosq)
#pragma intrinsic(_BitScanForward64)
#define REPEAT_QWORD_COPY(d,s,n) __stosq(d,s,n)
static inline unsigned long FORWARD_BIT_SCAN64(uint64_t v){
	unsigned long o;
	_BitScanForward64(&o,v);
	return o;
}
#else
static inline void REPEAT_QWORD_COPY(uint64_t* d,uint64_t* s,size_t n){
	__asm__("rep movsq":"=D"(d),"=S"(s),"=c"(n):"0"(d),"1"(s),"2"(n):"memory");
}
#define FORWARD_BIT_SCAN64(v) __builtin_ffs(v)
#endif



#define ALLOC_CHUNK_SIZE 4096



typedef struct __TREE_ELEM{
	uint8_t l;
	uint64_t m;
} tree_elem_t;



typedef struct __QUEUE_ELEM{
	uint64_t f;
	uint64_t bl[4];
} queue_elem_t;



typedef struct __TREE_DECODE_ELEM{
	char c;
	uint64_t m;
} tree_decode_elem_t;



typedef struct __TREE_ELEM_ARRAY{
	uint16_t l;
	tree_decode_elem_t* dt;
} tree_elem_array_t;



typedef struct __BUFFER_IO_CTX{
	uint64_t i;
	data_buffer_t* bf;
} buffer_io_ctx_t;



uint16_t _read_bf(void* ctx){
	buffer_io_ctx_t* io_ctx=(buffer_io_ctx_t*)ctx;
	if (io_ctx->i==io_ctx->bf->l){
		return COMPRESSOR_END_OF_DATA;
	}
	uint8_t e=*(io_ctx->bf->dt+io_ctx->i);
	io_ctx->i++;
	return e;
}



void _reset_read_bf(void* ctx){
	((buffer_io_ctx_t*)ctx)->i=0;
}



void _write_bf(void* ctx,uint8_t c){
	buffer_io_ctx_t* io_ctx=(buffer_io_ctx_t*)ctx;
	if (io_ctx->i==io_ctx->bf->l){
		io_ctx->bf->l+=ALLOC_CHUNK_SIZE;
		io_ctx->bf->dt=realloc(io_ctx->bf->dt,io_ctx->bf->l*sizeof(uint8_t));
	}
	*(io_ctx->bf->dt+io_ctx->i)=c;
	io_ctx->i++;
}



data_buffer_t* create_data_buffer(FILE* f){
	fseek(f,0,SEEK_END);
	uint64_t sz=ftell(f);
	rewind(f);
	data_buffer_t* o=malloc(sizeof(data_buffer_t));
	o->l=sz;
	o->dt=malloc(sz*sizeof(uint8_t));
	fread(o->dt,sizeof(uint8_t),sz,f);
	return o;
}



void compress_data(void* rf_ctx,compressor_data_read_func rf,compressor_data_reset_read_func rrf,void* wf_ctx,compressor_data_write_func wf){
	uint64_t fl[256];
	REPEAT_QWORD_COPY(fl,0,256);
	uint16_t ql=0;
	uint8_t si=0;
	uint64_t dtl=0;
	while (1){
		uint16_t e=rf(rf_ctx);
		if (e==COMPRESSOR_END_OF_DATA){
			break;
		}
		if (!fl[e]){
			si=(uint8_t)e;
			ql++;
		}
		fl[e]++;
		dtl++;
	}
	if (!dtl){
		return;
	}
	rrf(rf_ctx);
	tree_elem_t t[256];
	REPEAT_QWORD_COPY((uint64_t*)t,0,256*sizeof(tree_elem_t)/sizeof(uint64_t));
	uint8_t mx=1;
	if (ql==1){
		(t+si)->l=1;
	}
	else{
		queue_elem_t* q=malloc(ql*sizeof(queue_elem_t));
		uint8_t i=0;
		uint8_t j=0;
		while (1){
			if (fl[i]){
				queue_elem_t e={
					fl[i],
					{
						0,
						0,
						0,
						0
					}
				};
				e.bl[i>>6]=1ull<<(i&63);
				uint8_t k=j;
				while (k){
					uint8_t pi=(k-1)>>1;
					queue_elem_t pe=*(q+pi);
					if ((e.f!=pe.f?(e.f<pe.f):(e.bl[0]<pe.bl[0]||e.bl[1]<pe.bl[1]||e.bl[2]<pe.bl[2]||e.bl[3]<pe.bl[3]))){
						*(q+k)=pe;
						k=pi;
						continue;
					}
					break;
				}
				*(q+k)=e;
				if (j==ql-1){
					break;
				}
				j++;
			}
			if (i==255){
				break;
			}
			i++;
		}
		while (1){
			queue_elem_t ea=*q;
			ql--;
			queue_elem_t e=*(q+ql);
			uint16_t k=0;
			uint16_t ck=1;
			while (ck<ql){
				queue_elem_t ce=*(q+ck);
				if (ck+1<ql){
					queue_elem_t nce=*(q+ck+1);
					if ((ce.f!=nce.f?(ce.f>=nce.f):(ce.bl[0]>=nce.bl[0]||ce.bl[1]>=nce.bl[1]||ce.bl[2]>=nce.bl[2]||ce.bl[3]>=nce.bl[3]))){
						ck++;
						ce=*(q+ck);
					}
				}
				*(q+k)=ce;
				k=ck;
				ck=(k<<1)+1;
			}
			while (k){
				uint8_t pi=(k-1)>>1;
				queue_elem_t pe=*(q+pi);
				if ((e.f!=pe.f?(e.f<pe.f):(e.bl[0]<pe.bl[0]||e.bl[1]<pe.bl[1]||e.bl[2]<pe.bl[2]||e.bl[3]<pe.bl[3]))){
					*(q+k)=pe;
					k=pi;
					continue;
				}
				break;
			}
			*(q+k)=e;
			queue_elem_t eb=*q;
			for (i=0;i<4;i++){
				uint64_t m=ea.bl[i];
				while (m){
					unsigned long l=FORWARD_BIT_SCAN64(m);
					m&=~(1ull<<l);
					l+=i<<6;
					t[l].l++;
					if (t[l].l>=sizeof(uint64_t)*8){
						printf("Encoding won't fit in 64-bit integer!\n");
						return;
					}
					if (t[l].l>mx){
						mx=t[l].l;
					}
				}
				m=eb.bl[i];
				while (m){
					unsigned long l=FORWARD_BIT_SCAN64(m);
					m&=~(1ull<<l);
					l+=i<<6;
					t[l].m|=(1ull<<t[l].l);
					t[l].l++;
					if (t[l].l>=sizeof(uint64_t)*8){
						printf("Encoding won't fit in 64-bit integer!\n");
						return;
					}
					if (t[l].l>mx){
						mx=t[l].l;
					}
				}
			}
			if (ql==1){
				break;
			}
			e.f=ea.f+eb.f;
			e.bl[0]=ea.bl[0]|eb.bl[0];
			e.bl[1]=ea.bl[1]|eb.bl[1];
			e.bl[2]=ea.bl[2]|eb.bl[2];
			e.bl[3]=ea.bl[3]|eb.bl[3];
			k=0;
			ck=1;
			while (ck<ql){
				queue_elem_t ce=*(q+ck);
				if (ck+1<ql){
					queue_elem_t nce=*(q+ck+1);
					if ((ce.f!=nce.f?(ce.f>=nce.f):(ce.bl[0]>=nce.bl[0]||ce.bl[1]>=nce.bl[1]||ce.bl[2]>=nce.bl[2]||ce.bl[3]>=nce.bl[3]))){
						ck++;
						ce=*(q+ck);
					}
				}
				*(q+k)=ce;
				k=ck;
				ck=(k<<1)+1;
			}
			while (k){
				uint8_t pk=(k-1)>>1;
				queue_elem_t pe=*(q+pk);
				if ((e.f!=pe.f?(e.f<pe.f):(e.bl[0]<pe.bl[0]||e.bl[1]<pe.bl[1]||e.bl[2]<pe.bl[2]||e.bl[3]<pe.bl[3]))){
					*(q+k)=pe;
					k=pk;
					continue;
				}
				break;
			}
			*(q+k)=e;
		}
		free(q);
	}
	uint8_t i=0;
	if (dtl>UINT32_MAX){
		i=3;
	}
	else if (dtl>UINT16_MAX){
		i=2;
	}
	else if (dtl>UINT8_MAX){
		i=1;
	}
	wf(wf_ctx,(i<<6)|(mx-1));
	i=1<<(i+3);
	while (i){
		i-=8;
		wf(wf_ctx,(uint8_t)(dtl>>i));
	}
	i=0;
	while (1){
		tree_elem_t e=t[i];
		uint8_t j=((e.l+7)>>3)<<3;
		wf(wf_ctx,e.l);
		while (j){
			j-=8;
			wf(wf_ctx,(uint8_t)(e.m>>j));
		}
		if (i==255){
			break;
		}
		i++;
	}
	uint32_t bf=0;
	uint8_t bfl=0;
	for (uint64_t k=0;k<dtl;k++){
		tree_elem_t e=t[rf(rf_ctx)];
		uint8_t l=e.l;
		while (l){
			uint8_t m=(l>16?16:l);
			l-=m;
			bf=(bf<<m)|((e.m>>l)&0xffff);
			bfl+=m;
			while (bfl>=8){
				bfl-=8;
				wf(wf_ctx,(uint8_t)(bf>>bfl));
			}
		}
	}
	if (bfl){
		wf(wf_ctx,(uint8_t)(bf<<(8-bfl)));
	}
}



data_buffer_t* compress_data_bf(data_buffer_t* dt){
	data_buffer_t* o=malloc(sizeof(data_buffer_t));
	o->l=0;
	o->dt=NULL;
	buffer_io_ctx_t r_ctx={
		0,
		dt
	};
	buffer_io_ctx_t w_ctx={
		0,
		o
	};
	compress_data((void*)&r_ctx,_read_bf,_reset_read_bf,(void*)&w_ctx,_write_bf);
	o->l=w_ctx.i;
	o->dt=realloc(o->dt,o->l*sizeof(uint8_t));
	return o;
}



void decompress_data(void* rf_ctx,compressor_data_read_func rf,void* wf_ctx,compressor_data_write_func wf){
	uint8_t f=(uint8_t)rf(rf_ctx);
	uint8_t tl=(f&0x3f)+1;
	f=1<<((f>>6)+3);
	uint64_t ol=0;
	while (f){
		f-=8;
		ol=(ol<<8)|rf(rf_ctx);
	}
	tree_elem_array_t* t=malloc(tl*sizeof(tree_elem_array_t)+(tl+1)*sizeof(uint8_t));
	REPEAT_QWORD_COPY((uint64_t*)t,0,tl*sizeof(tree_elem_array_t)/sizeof(uint64_t));
	uint8_t j=0;
	while (1){
		uint8_t l=(uint8_t)rf(rf_ctx);
		if (l){
			uint8_t k=l-1;
			l=(l+7)/8;
			uint64_t m=0;
			while (l){
				l--;
				m=(m<<8)|rf(rf_ctx);
			}
			(t+k)->l++;
			(t+k)->dt=realloc((t+k)->dt,(t+k)->l*sizeof(tree_decode_elem_t));
			tree_decode_elem_t* e=(t+k)->dt+(t+k)->l-1;
			e->c=j;
			e->m=m;
		}
		if (j==255){
			break;
		}
		j++;
	}
	uint8_t* ti=(uint8_t*)(void*)((uint64_t)(void*)t+tl*sizeof(tree_elem_array_t));
	for (j=0;j<tl+1;j++){
		uint8_t k=j+1;
		while (k<tl+1&&(!k||!(t+k-1)->l)){
			k++;
		}
		*(ti+j)=k-j;
	}
	uint32_t bf=0;
	uint8_t bfl=0;
	j=0;
	uint16_t e=0;
	uint64_t i=0;
	while (1){
		if (!j){
			e=rf(rf_ctx);
			if (e==COMPRESSOR_END_OF_DATA){
				break;
			}
			j=8;
		}
		uint8_t k=*(ti+bfl);
		if (k>j){
			k=j;
		}
		j-=k;
		bf=(bf<<k)|((e>>j)&((1<<k)-1));
		bfl+=k;
		tree_elem_array_t* a=t+bfl-1;
		for (uint16_t l=0;l<a->l;l++){
			tree_decode_elem_t de=*(a->dt+l);
			if (de.m==bf){
				wf(wf_ctx,de.c);
				i++;
				bf=0;
				bfl=0;
				break;
			}
		}
		if (i==ol){
			break;
		}
	}
	while (tl){
		tl--;
		if ((t+tl)->l){
			free((t+tl)->dt);
		}
	}
	free(t);
}



data_buffer_t* decompress_data_bf(data_buffer_t* dt){
	data_buffer_t* o=malloc(sizeof(data_buffer_t));
	o->l=0;
	o->dt=NULL;
	buffer_io_ctx_t r_ctx={
		0,
		dt
	};
	buffer_io_ctx_t w_ctx={
		0,
		o
	};
	decompress_data((void*)&r_ctx,_read_bf,(void*)&w_ctx,_write_bf);
	o->l=w_ctx.i;
	o->dt=realloc(o->dt,o->l*sizeof(uint8_t));
	return o;
}



void free_data_buffer(data_buffer_t* dt){
	if (dt->l){
		free(dt->dt);
	}
	free(dt);
}
