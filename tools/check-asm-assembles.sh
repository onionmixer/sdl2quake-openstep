#!/bin/sh
# Will OPENSTEP's assembler take Quake's hand-written renderer?
#
# Run ON the target:  sh /ndrv/openstep-quake/tools/check-asm-assembles.sh <dir>
#
# The question is not academic.  Thirteen of Quake's twenty-one .S files ARE
# the fast software renderer -- span drawing, edge processing, surface
# blocks, the Alias transform -- and every one has a C twin under
# `#if !id386`.  Deciding to use the C twins is reasonable; deciding it
# because "the assembler probably will not work" is a guess, and this
# replaces the guess with an answer.
#
# What favours it: asm_i386.h reduces to `#define C(label) _##label` when ELF
# is not defined, which is exactly Mach-O's underscore convention, and the
# files use only .globl/.align/.text/.data/.long.
#
# WHAT THIS DOES NOT PROVE.  Assembling is not working.  The .S files carry
# hand-written struct offsets in asm_i386.h, and those are literal constants
# -- they assemble whether or not they match what this compiler lays out for
# the corresponding C structs.  Checking that is a separate job, and until it
# is done a clean assemble means the SYNTAX is accepted, nothing more.
#
# The three DOS/Windows-only files (dosasm.S, sys_dosa.S, sys_wina.S) are not
# built by any target of ours, so they are not copied into the test dir.
cd "${1:-.}" || exit 2
ok=0; bad=0
for f in *.S; do
    o=/tmp/asmtest_`basename $f .S`.o
    rm -f $o
    cc -c -m486 -DUSE_ASM $f -o $o 2> /tmp/asmtest_err
    if [ -f $o ]; then
        ok=`expr $ok + 1`
        echo "  ok    $f"
    else
        bad=`expr $bad + 1`
        echo "  FAIL  $f"
        sed -n '1,3p' /tmp/asmtest_err | sed 's/^/          /'
    fi
done
echo ""
echo "ASM_ASSEMBLE ok=$ok fail=$bad"
