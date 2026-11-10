.. SPDX-License-Identifier: GPL-2.0

Linux 6.6 multi-gen LRU on the 4.19 Samsung baseline
=================================================

Source
------

The generation aging, eviction, tier/refault feedback, memcg LRU, working-set
protection and control interfaces are based on Linux v6.6, commit
``ffc253263a1375a65fa6c9f62a893e9767fbebfa``. The original implementation is
by Yu Zhao and the Linux memory-management contributors.

Native interfaces
-----------------

This backport uses the existing ``struct page`` interfaces, linked VMA lists,
per-node LRU locks and mmap read locking. PTE tables remain protected by the
4.19 mmap lock and native PTE mapping/locking protocol. Page flag updates use
the native cmpxchg API. Memory-tier demotion and proactive scan_control fields
are outside this baseline; ordinary swap and debugfs reclaim remain available.

The 4.19 memcg charge/commit order and aggregate workingset statistics are
retained. The per-node memcg LRU is published and removed with the CSS lifetime,
including online, offline, release and soft-limit feedback. The classic LRU
remains selectable with the runtime kill switch or CONFIG_LRU_GEN disabled.
The existing kswapd retry limit still accounts for reclaim without progress.
Memcg reclaim owns a scoped walk state, since the native 4.19 entry point
does not install one. The previous task state is restored on return.

Swap refaults
-------------

Linux 6.6 charges swapin pages before publishing them in swapcache. This kernel
commits the charge in do_swap_page(), after swapcache insertion and readahead.
Eviction tokens are therefore retained in a per-swap-area XArray until a real
fault has committed the page's charge. This also retains tokens for pages that
were prefetched before the fault. Each token is consumed once, after charging
and before the PTE's swap reference is released.

Tokens cover each base-page slot of a reclaimed compound page. They are removed
before slots are reused and the array is destroyed on successful swapoff.
Metadata allocation during eviction is nonblocking and best effort. It never
initiates recursive reclaim. A prefetched page that remains uncharged does not
contribute an eviction token. Refault feedback is counted at the first actual
fault rather than at speculative swapcache population.

See ``Documentation/admin-guide/mm/multigen_lru.rst`` for runtime controls.
