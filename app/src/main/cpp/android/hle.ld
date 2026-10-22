/* Linker fragment (lld): the Android link runs with --no-undefined (AGP), which skips lld's
 * automatic __start_X/__stop_X synthesis for C-identifier section names. Define them here,
 * bracketing the section data exactly. (Upstream relies on the synthesis; see runtime/hle.h and
 * runtime/main.c's RT_ON_RESET table.) */
SECTIONS {
  .sw_hle : {
    __start_hle_impls = .;
    KEEP(*(hle_impls))
    __stop_hle_impls = .;
  }
  .sw_resets : {
    __start_resets = .;
    KEEP(*(resets))
    __stop_resets = .;
  }
} INSERT AFTER .bss;
