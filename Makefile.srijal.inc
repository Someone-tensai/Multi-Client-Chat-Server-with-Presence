# Makefile.srijal.inc — Srijal's source files for all Milestones
# At integration, Janak runs: cat Makefile.srijal.inc >> Makefile

# Milestone 1: Session & Realtime
src/session.c
src/receipt.c
src/presence.c

# Milestone 2: Moderation & Security
src/block.c
src/permission.c
src/invite.c

# Milestone 3: Distributed
src/redis.c
src/pg.c
src/metrics.c
