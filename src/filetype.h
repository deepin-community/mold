#pragma once

#include "../lib/common.h"
#include "elf.h"

namespace mold {

enum class FileType {
  UNKNOWN,
  EMPTY,
  ELF_OBJ,
  ELF_DSO,
  AR,
  THIN_AR,
  TEXT,
  GCC_LTO_OBJ,
  LLVM_BITCODE,
};

inline bool is_text_file(MappedFile *mf) {
  auto istext = [](char c) {
    return isprint(c) || c == '\n' || c == '\t';
  };

  u8 *data = mf->data;
  return mf->size >= 4 && istext(data[0]) && istext(data[1]) &&
         istext(data[2]) && istext(data[3]);
}

template <typename E>
inline bool is_gcc_lto_obj(MappedFile *mf, bool has_plugin) {
  const char *data = mf->get_contents().data();
  ElfEhdr<E> &ehdr = *(ElfEhdr<E> *)data;
  ElfShdr<E> *sh_begin = (ElfShdr<E> *)(data + ehdr.e_shoff);
  std::span<ElfShdr<E>> shdrs{(ElfShdr<E> *)(data + ehdr.e_shoff), ehdr.e_shnum};

  // e_shstrndx is a 16-bit field. If .shstrtab's section index is
  // too large, the actual number is stored to sh_link field.
  i64 shstrtab_idx = (ehdr.e_shstrndx == SHN_XINDEX)
    ? sh_begin->sh_link : ehdr.e_shstrndx;

  for (ElfShdr<E> &sec : shdrs) {
    // GCC FAT LTO objects contain both regular ELF sections and GCC-
    // specific LTO sections, so that they can be linked as LTO objects if
    // the LTO linker plugin is available and falls back as regular
    // objects otherwise. GCC FAT LTO object can be identified by the
    // presence of `.gcc.lto_.symtab` section.
    if (has_plugin) {
      std::string_view name = data + shdrs[shstrtab_idx].sh_offset + sec.sh_name;
      if (name.starts_with(".gnu.lto_.symtab."))
        return true;
    }

    if (sec.sh_type != SHT_SYMTAB)
      continue;

    // GCC non-FAT LTO object contains only sections symbols followed by
    // a common symbol whose name is `__gnu_lto_slim` (or `__gnu_lto_v1`
    // for older GCC releases).
    std::span<ElfSym<E>> elf_syms{(ElfSym<E> *)(data + sec.sh_offset),
                                  (size_t)sec.sh_size / sizeof(ElfSym<E>)};

    auto skip = [](u8 type) {
      return type == STT_NOTYPE || type == STT_FILE || type == STT_SECTION;
    };

    i64 i = 1;
    while (i < elf_syms.size() && skip(elf_syms[i].st_type))
      i++;

    if (i < elf_syms.size() && elf_syms[i].st_shndx == SHN_COMMON) {
      std::string_view name =
        data + shdrs[sec.sh_link].sh_offset + elf_syms[i].st_name;
      if (name.starts_with("__gnu_lto_"))
        return true;
    }
    break;
  }

  return false;
}

template <typename E>
FileType get_file_type(Context<E> &ctx, MappedFile *mf) {
  std::string_view data = mf->get_contents();
  bool has_plugin = !ctx.arg.plugin.empty();

  if (data.empty())
    return FileType::EMPTY;

  if (data.starts_with("\177ELF")) {
    u8 byte_order = ((ElfEhdr<I386> *)data.data())->e_ident[EI_DATA];

    if (byte_order == ELFDATA2LSB) {
      auto &ehdr = *(ElfEhdr<I386> *)data.data();

      if (ehdr.e_type == ET_REL) {
        if (ehdr.e_ident[EI_CLASS] == ELFCLASS32) {
          if (is_gcc_lto_obj<I386>(mf, has_plugin))
            return FileType::GCC_LTO_OBJ;
        } else {
          if (is_gcc_lto_obj<X86_64>(mf, has_plugin))
            return FileType::GCC_LTO_OBJ;
        }
        return FileType::ELF_OBJ;
      }

      if (ehdr.e_type == ET_DYN)
        return FileType::ELF_DSO;
    } else {
      auto &ehdr = *(ElfEhdr<M68K> *)data.data();

      if (ehdr.e_type == ET_REL) {
        if (ehdr.e_ident[EI_CLASS] == ELFCLASS32) {
          if (is_gcc_lto_obj<M68K>(mf, has_plugin))
            return FileType::GCC_LTO_OBJ;
        } else {
          if (is_gcc_lto_obj<SPARC64>(mf, has_plugin))
            return FileType::GCC_LTO_OBJ;
        }
        return FileType::ELF_OBJ;
      }

      if (ehdr.e_type == ET_DYN)
        return FileType::ELF_DSO;
    }
    return FileType::UNKNOWN;
  }

  if (data.starts_with("!<arch>\n"))
    return FileType::AR;
  if (data.starts_with("!<thin>\n"))
    return FileType::THIN_AR;
  if (is_text_file(mf))
    return FileType::TEXT;
  if (data.starts_with("\xde\xc0\x17\x0b"))
    return FileType::LLVM_BITCODE;
  if (data.starts_with("BC\xc0\xde"))
    return FileType::LLVM_BITCODE;
  return FileType::UNKNOWN;
}

inline std::ostream &operator<<(std::ostream &out, FileType type) {
  auto to_string = [&] {
    switch (type) {
    case FileType::UNKNOWN: return "UNKNOWN";
    case FileType::EMPTY: return "EMPTY";
    case FileType::ELF_OBJ: return "ELF_OBJ";
    case FileType::ELF_DSO: return "ELF_DSO";
    case FileType::AR: return "AR";
    case FileType::THIN_AR: return "THIN_AR";
    case FileType::TEXT: return "TEXT";
    case FileType::GCC_LTO_OBJ: return "GCC_LTO_OBJ";
    case FileType::LLVM_BITCODE: return "LLVM_BITCODE";
    default: return "UNKNOWN";
    }
  };

  out << to_string();
  return out;
}

} // namespace mold
