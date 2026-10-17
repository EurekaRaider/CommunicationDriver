# The pinned libssh2 1.11.1 ECDH/Curve25519 paths overwrite server_hostkey
# during rekeying. Match the existing DH path's ownership before replacement.
function(cd_patch_libssh2 source_dir)
 set(path "${source_dir}/src/kex.c")
 file(READ "${path}" original)
 set(content "${original}")
 set(release "        /* CommunicationDriver: release owned host key before rekeying. */
        if(session->server_hostkey) {
            LIBSSH2_FREE(session, session->server_hostkey);
            session->server_hostkey = NULL;
            session->server_hostkey_len = 0;
        }

")
 foreach(anchor IN ITEMS
   "        if(_libssh2_copy_string(session, &buf, &(session->server_hostkey),\n                                &server_public_key_len)) {"
   "        session->server_hostkey_len = (uint32_t)hostkey_len;")
  string(FIND "${content}" "${release}${anchor}" already_patched)
  if(already_patched EQUAL -1)
   string(FIND "${content}" "${anchor}" position)
   if(position EQUAL -1)
    message(FATAL_ERROR "Pinned libssh2 host-key patch no longer matches; review dependency source")
   endif()
   string(REPLACE "${anchor}" "${release}${anchor}" content "${content}")
  endif()
 endforeach()
 if(NOT content STREQUAL original)
  file(WRITE "${path}" "${content}")
 endif()
 # OpenSSL 3+ ECDH owns all three temporary objects, separately from the
 # borrowed private key. The original cleanup only released server_key_ctx.
 set(path "${source_dir}/src/openssl.c")
 file(READ "${path}" original)
 set(anchor "    if(server_key_ctx)
        EVP_PKEY_CTX_free(server_key_ctx);
#else
    if(server_public_key_point)")
 set(replacement "    if(server_key_ctx)
        EVP_PKEY_CTX_free(server_key_ctx);

    /* CommunicationDriver: release owned OpenSSL ECDH temporaries. */
    if(peer_key)
        EVP_PKEY_free(peer_key);
    if(key_fromdata_ctx)
        EVP_PKEY_CTX_free(key_fromdata_ctx);
    if(bn_ctx)
        BN_CTX_free(bn_ctx);
#else
    if(server_public_key_point)")
 string(FIND "${original}" "${replacement}" already_patched)
 if(already_patched EQUAL -1)
  string(FIND "${original}" "${anchor}" position)
  if(position EQUAL -1)
   message(FATAL_ERROR "Pinned libssh2 OpenSSL ECDH patch no longer matches; review dependency source")
  endif()
  string(REPLACE "${anchor}" "${replacement}" content "${original}")
  file(WRITE "${path}" "${content}")
 endif()
endfunction()
