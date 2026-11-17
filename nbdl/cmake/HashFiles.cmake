# Write the SHA256 of each of FILES to STAMP, keeping its time if unchanged.
set(hashes "")
foreach(file IN LISTS FILES)
  file(SHA256 ${file} hash)
  string(APPEND hashes "${hash}  ${file}\n")
endforeach()
file(WRITE ${STAMP}.tmp "${hashes}")
file(COPY_FILE ${STAMP}.tmp ${STAMP} ONLY_IF_DIFFERENT)
