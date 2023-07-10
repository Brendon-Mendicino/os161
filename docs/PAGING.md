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

T