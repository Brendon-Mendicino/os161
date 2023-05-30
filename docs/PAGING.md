# Paging

## TLB Support

TLB avra' il supporto per la gestione dell'_address space_ a cui appartiene,
questo verra' ottenuto abilitando il supporto per il campo `ASID` (Address
Space ID), all'interno della parte `EntryHi` con i bit da 0 a 7 della TLB.

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
  |   +-------+           |   +-------+                            
  |   |       |           |   |       |                            
  |   |-------|           |   |-------|                            
  |   |       |           |   |       |                            
  |   |-------|           |   |-------|                            
  |   |       |           \-->|       |                            
  |   |-------|               |-------|                            
  |   |pte_t *|-----\         |       |                            
  \-->|-------|     |         |-------|                            
      |       |     |         |       |                            
  /-->+-------+     \-------->+-------+                            
  |                                                            
  |                                                            
pmd_t *
```