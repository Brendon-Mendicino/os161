# Paging

## TLB Support

Ogni volta che avviene un context switch c'e' uno svuotamento delle TLB,
infatti in OS161 non esiste il supporto per il campo ASID all'interno della
`EntryHi`, ogni volta che avviene un fault nella TLB vengono possono essere per
diverse casistiche
- le entry non esiste
- si tenta di scrivere su una entry che ha il ha campo D settato a falso
- si tenta di leggere o scirvere una pagina non valida

### Entry non esiste

Se la entry non esiste nella tabella si va a cercare l'indirizzo fisico nella
Page Table del rispettivo processo, si prende il valore dal PFN 
(*Page Frame Number*) da `pte_t` e si inserisce all'interno della TLB, se la 
pagina non esiste all'interno della Page Table viene caricato dal disco alla
memoria attraverso On-Demand Page Loading.

### ...

## On-Demand Page Loading


## Page Replacement


## 2-Level Page Table

La page table sara' messa all'interno della `struct proc` ed avra' una struttura a
due livelli, dividendo l'indirizzo virtuale in due livelli rispettivamente in
```
| [31 ------------ 22 ] | [ 21 ------ 12 ] | [11 ---- 0] |
| Page Middle Directory | Paga Table Entry | Page Offset |
```
le parti di indirizzo forniranno l'offset all'interno delle tabelle di livello
che dovranno essere contigue
```
| [31 ------------ 22 ] | [ 21 ------ 12 ] | [11 ---- 0] |
| Page Middle Directory | Paga Table Entry | Page Offset |
  |                       |                               
  |                       |                               
  pmd_offset              pte_offset                               
  |   pmd                 |   pte                         
  |   +-------+           |   +-------+     page                       
  |   |       |           |   |       |     +-------+                       
  |   |-------|           |   |-------|     |       |                       
  |   |       |           |   |       |     |       |                       
  |   |-------|           |   |-------|     |       |                       
  |   |       |           \-->|       |---->+-------+                           
  |   |-------|               |-------|                            
  |   |       |-----\         |       |                            
  \-->|-------|     |         |-------|                            
      |       |     |         |       |                            
  /-->+-------+     \-------->+-------+                            
  |                                                            
  |                                                            
pmd_t *
```

La `struct proc` avra' un campo `pmd_t *pmt` che punta al primo livello della tabella.


## Atomic

Il supporto per gli e' implementato in assembly, il codice e' molto simile a quello del `testandset` dello `spinlock`, nella `atomic_fetch_add` vengono prese delle precauzioni:
- il codice assembly viene preceduto da una `membar` (l'istruzione `sync`) e viene anche suseguito da una barriera di memoria (il *clobber* `"memory"` che fa parte della sintassi di gcc)
- la `llsc` puo' sempre fallire, se cio' accade si ripete la sezione di codice, questo viene fatto grazie ad un istruzione di jump
- l'incremento del contatore e' visibile a tutte le cpu in modo non ordinato rispetto alla cpu che chiama la prima istruzione di `ll`, questo perche' se un altra cpu cerca di scrivere nella zona di memoria la prima fallira'

```c
static inline int
atomic_fetch_add(atomic_t *atomic, int val)
{
    int temp;
    int result;

    asm volatile(
    "    .set push;"          /* save assembler mode */
    "    .set mips32;"        /* allow MIPS32 instructions */
    "    sync;"               /* memory barrier for previous read/write */
    "    .set volatile;"      /* avoid unwanted optimization */
    "1:  ll   %1, 0(%2);"     /*   temp = atomic->val */
    "    add  %0, %1, %3;"    /*   result = temp + val */
    "    sc   %0, 0(%2);"     /*   *sd = result; result = success? */
    "    beqz %0, 1b;"
    "    .set pop;"           /* restore assembler mode */
    "    move %0, %1;"        /*   result = temp */
    : "=&r" (result), "=&r" (temp)
    : "r" (&atomic->counter), "Ir" (val)
    : "memory");              /* memory barrier for the current assembly block */

    return result;
}
```