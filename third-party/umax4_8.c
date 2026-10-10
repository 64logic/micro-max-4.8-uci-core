/***************************************************************************/
/*                               micro-Max,                                */
/* A chess program smaller than 2KB (of non-blank source), by H.G. Muller  */
/***************************************************************************/
/* version 4.8 (1953 characters) features:                                 */
/* - recursive negamax search                                              */
/* - all-capture MVV/LVA quiescence search                                 */
/* - (internal) iterative deepening                                        */
/* - best-move-first 'sorting'                                             */
/* - a hash table storing score and best move                              */
/* - futility pruning                                                      */
/* - king safety through magnetic, frozen king in middle-game              */
/* - R=2 null-move pruning                                                 */
/* - keep hash and repetition-draw detection                               */
/* - better defense against passers through gradual promotion              */
/* - extend check evasions in inner nodes                                  */
/* - reduction of all non-Pawn, non-capture moves except hash move (LMR)   */
/* - full FIDE rules (expt under-promotion) and move-legality checking     */

#define W while
#define K(A,B) *(int*)(T+A+(B&8)+S*(B&7))
#define J(A) K(y+A,b[y])-K(x+A,u)-K(H+A,t)

#define U (1<<24)
struct _ {int K,V;char X,Y,D;} A[U];           /* hash table, 16M+8 entries*/

int M=136,S=128,I=8e3,Q,O,K,N,R,J,Z,k=16,*p,c[9]; /* M=0x88                */

char L,
w[]={0,2,2,7,-1,8,12,23},                      /* relative piece values    */
o[]={-16,-15,-17,0,1,16,0,1,16,15,17,0,14,18,31,33,0, /* step-vector lists */
     7,-1,11,6,8,3,6,                          /* 1st dir. in o[] per piece*/
     6,3,5,7,4,5,3,6},                         /* initial piece setup      */
b[129],                                        /* board: half of 16x8+dummy*/
T[1035];                                       /* hash translation table   */

D(q,l,e,E,z,n)          /* recursive minimax search, k=moving side, n=depth*/
int q,l,e,E,z,n;        /* (q,l)=window, e=current eval. score, E=e.p. sqr.*/
{                       /* e=score, z=prev.dest; J,Z=hashkeys; return score*/
 int j,r,m,v,d,h,i,F,G,V,P,f=J,g=Z,C,s;
 char t,p,u,x,y,X,Y,H,B;
 struct _*a=A+(J+k*E&U-1);                     /* lookup pos. in hash table*/

 q--;                                          /* adj. window: delay bonus */
 k^=24;                                        /* change sides             */
 d=a->D;m=a->V;X=a->X;Y=a->Y;                  /* resume at stored depth   */
 if(a->K-Z|z|                                  /* miss: other pos. or empty*/
  !(m<=q|X&8&&m>=l|X&S))                       /*   or window incompatible */
  d=Y=0;                                       /* start iter. from scratch */
 X&=~M;                                        /* start at best-move hint  */

 W(d++<n||d<3||                                /* iterative deepening loop */
   z&K==I&&(N<1e6&d<98||                       /* root: deepen upto time   */
   (K=X,L=Y&~M,d=3)))                          /* time's up: go do best    */
 {x=B=X;                                       /* start scan at prev. best */
  h=Y&S;                                       /* request try noncastl. 1st*/
  P=d<3?I:D(-l,1-l,-e,S,0,d-3);                /* Search null move         */
  m=-P<l|R>35?d>2?-I:e:-P;                     /* Prune or stand-pat       */
  N++;                                         /* node count (for timing)  */
  do{u=b[x];                                   /* scan board looking for   */
   if(u&k)                                     /*  own piece (inefficient!)*/
   {r=p=u&7;                                   /* p = piece type (set r>0) */
    j=o[p+16];                                 /* first step vector f.piece*/
    W(r=p>2&r<0?-r:-o[++j])                    /* loop over directions o[] */
    {A:                                        /* resume normal after best */
     y=x;F=G=S;                                /* (x,y)=move, (F,G)=castl.R*/
     do{                                       /* y traverses ray, or:     */
      H=y=h?Y^h:y+r;                           /* sneak in prev. best move */
      if(y&M)break;                            /* board edge hit           */
      m=E-S&b[E]&&y-E<2&E-y<2?I:m;             /* bad castling             */
      if(p<3&y==E)H^=16;                       /* shift capt.sqr. H if e.p.*/
      t=b[H];if(t&k|p<3&!(y-x&7)-!t)break;     /* capt. own, bad pawn mode */
      i=37*w[t&7]+(t&192);                     /* value of capt. piece t   */
      m=i<0?I:m;                               /* K capture                */
      if(m>=l&d>1)goto C;                      /* abort on fail high       */

      v=d-1?e:i-p;                             /* MVV/LVA scoring          */
      if(d-!t>1)                               /* remaining depth          */
      {v=p<6?b[x+8]-b[y+8]:0;                  /* center positional pts.   */
       b[G]=b[H]=b[x]=0;b[y]=u|32;             /* do move, set non-virgin  */
       if(!(G&M))b[F]=k+6,v+=50;               /* castling: put R & score  */
       v-=p-4|R>29?0:20;                       /* penalize mid-game K move */
       if(p<3)                                 /* pawns:                   */
       {v-=9*((x-2&M||b[x-2]-u)+               /* structure, undefended    */
              (x+2&M||b[x+2]-u)-1              /*        squares plus bias */
             +(b[x^16]==k+36))                 /* kling to non-virgin King */
             -(R>>2);                          /* end-game Pawn-push bonus */
        V=y+r+1&S?647-p:2*(u&y+16&32);         /* promotion or 6/7th bonus */
        b[y]+=V;i+=V;                          /* change piece, add score  */
       }
       v+=e+i;V=m>q?m:q;                       /* new eval and alpha       */
       J+=J(0);Z+=J(8)+G-S;                    /* update hash key          */
       C=d-1-(d>5&p>2&!t&!h);
       C=R>29|d<3|P-I?C:d;                     /* extend 1 ply if in check */
       do
        s=C>2|v>V?-D(-l,-V,-v,                 /* recursive eval. of reply */
                              F,0,C):v;        /* or fail low if futile    */
       W(s>q&++C<d);v=s;
       if(z&&K-I&&v+I&&x==K&y==L)              /* move pending & in root:  */
       {Q=-e-i;O=F;                            /*   exit if legal & found  */
        a->D=99;a->V=0;                        /* lock game in hash as draw*/
        R+=i>>7;return l;                      /* captured non-P material  */
       }
       J=f;Z=g;                                /* restore hash key         */
       b[G]=k+6;b[F]=b[y]=0;b[x]=u;b[H]=t;     /* undo move,G can be dummy */
      }
      if(v>m)                                  /* new best, update max,best*/
       m=v,X=x,Y=y|S&F;                        /* mark double move with S  */
      if(h){h=0;goto A;}                       /* redo after doing old best*/
      if(x+r-y|u&32|                           /* not 1st step,moved before*/
         p>2&(p-4|j-7||                        /* no P & no lateral K move,*/
         b[G=x+3^r>>1&7]-k-6                   /* no virgin R in corner G, */
         ||b[G^1]|b[G^2])                      /* no 2 empty sq. next to R */
        )t+=p<5;                               /* fake capt. for nonsliding*/
      else F=y;                                /* enable e.p.              */
     }W(!t);                                   /* if not capt. continue ray*/
  }}}W((x=x+9&~M)-B);                          /* next sqr. of board, wrap */
C:if(m>I-M|m<M-I)d=98;                         /* mate holds to any depth  */
  m=m+I|P==I?m:0;                              /* best loses K: (stale)mate*/
  if(a->D<99)                                  /* protect game history     */
   a->K=Z,a->V=m,a->D=d,                       /* always store in hash tab */
   a->X=X|8*(m>q)|S*(m<l),a->Y=Y;              /* move, type (bound/exact),*/
/*if(z)printf("%2d ply, %9d searched, score=%6d by %c%c%c%c\n",d-1,N-S,m,
     'a'+(X&7),'8'-(X>>4),'a'+(Y&7),'8'-(Y>>4&7)); /* uncomment for Kibitz */
 }                                             /*    encoded in X S,8 bits */
 k^=24;                                        /* change sides back        */
 return m+=m<e;                                /* delayed-loss bonus       */
}

main()
{
 K=8;W(K--) 
 {b[K]=(b[K+112]=o[K+24]+8)+8;b[K+16]=18;b[K+96]=9;  /* initial board setup*/
  L=8;W(L--)b[16*L+K+8]=(K-4)*(K-4)+(L-3.5)*(L-3.5); /* center-pts table   */
 }                                                   /*(in unused half b[])*/
 N=1035;W(N-->M)T[N]=rand()>>9;

 W(1)                                                /* play loop          */
 {N=-1;W(++N<121)
   printf("%c",N&8&&(N+=7)?10:".?+nkbrq?*?NKBRQ"[b[N]&15]); /* print board */
  p=c;W((*p++=getchar())>10);                        /* read input line    */
  K=I;                                               /* invalid move       */
  if(*c-10)K=*c-16*c[1]+799,L=c[2]-16*c[3]+799;      /* parse entered move */
  D(-I,I,Q,O,1,3);                                   /* think or check & do*/
 }
}
