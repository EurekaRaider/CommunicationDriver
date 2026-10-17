# The pinned libssh2 1.11.1 ECDH/Curve25519 paths overwrite server_hostkey during rekeying. Match
# the existing DH path's ownership before replacement.
function(cd_patch_libssh2_fragment variable anchor replacement)
    set(content "${${variable}}")
    string(FIND "${content}" "${replacement}" already_patched)
    if(already_patched EQUAL -1)
        string(FIND "${content}" "${anchor}" position)
        if(position EQUAL -1)
            message(
                FATAL_ERROR
                    "Pinned libssh2 WinCNG patch no longer matches; review dependency source"
            )
        endif()
        string(REPLACE "${anchor}" "${replacement}" content "${content}")
    endif()

    set(${variable}
        "${content}"
        PARENT_SCOPE
    )
endfunction()

function(cd_patch_libssh2 source_dir)
    set(path "${source_dir}/src/kex.c")
    file(READ "${path}" original)
    set(content "${original}")
    set(release
        "        /* CommunicationDriver: release owned host key before rekeying. */
        if(session->server_hostkey) {
            LIBSSH2_FREE(session, session->server_hostkey);
            session->server_hostkey = NULL;
            session->server_hostkey_len = 0;
        }

"
    )
    foreach(
        anchor IN
        ITEMS
            "        if(_libssh2_copy_string(session, &buf, &(session->server_hostkey),\n                                &server_public_key_len)) {"
            "        session->server_hostkey_len = (uint32_t)hostkey_len;"
    )
        string(FIND "${content}" "${release}${anchor}" already_patched)
        if(already_patched EQUAL -1)
            string(FIND "${content}" "${anchor}" position)
            if(position EQUAL -1)
                message(
                    FATAL_ERROR
                        "Pinned libssh2 host-key patch no longer matches; review dependency source"
                )
            endif()
            string(REPLACE "${anchor}" "${release}${anchor}" content "${content}")
        endif()
    endforeach()
    if(NOT content STREQUAL original)
        file(WRITE "${path}" "${content}")
    endif()

    # OpenSSL 3+ ECDH owns all three temporary objects, separately from the borrowed private key.
    # The original cleanup only released server_key_ctx.
    set(path "${source_dir}/src/openssl.c")
    file(READ "${path}" original)
    set(anchor
        "    if(server_key_ctx)
        EVP_PKEY_CTX_free(server_key_ctx);
#else
    if(server_public_key_point)"
    )
    set(replacement
        "    if(server_key_ctx)
        EVP_PKEY_CTX_free(server_key_ctx);

    /* CommunicationDriver: release owned OpenSSL ECDH temporaries. */
    if(peer_key)
        EVP_PKEY_free(peer_key);
    if(key_fromdata_ctx)
        EVP_PKEY_CTX_free(key_fromdata_ctx);
    if(bn_ctx)
        BN_CTX_free(bn_ctx);
#else
    if(server_public_key_point)"
    )
    string(FIND "${original}" "${replacement}" already_patched)
    if(already_patched EQUAL -1)
        string(FIND "${original}" "${anchor}" position)
        if(position EQUAL -1)
            message(
                FATAL_ERROR
                    "Pinned libssh2 OpenSSL ECDH patch no longer matches; review dependency source"
            )
        endif()
        string(REPLACE "${anchor}" "${replacement}" content "${original}")
        file(WRITE "${path}" "${content}")
    endif()

    # The filtered list follows server order and can contain repeated algorithms. Sizing it from the
    # shorter local list can overflow during public-key auth.
    set(path "${source_dir}/src/userauth.c")
    file(READ "${path}" original)
    set(anchor "    filtered_algs = LIBSSH2_ALLOC(session, strlen(supported_algs) + 1);")
    set(replacement
        "    /* CommunicationDriver: allow repeated server signature algorithms. */
    filtered_algs = LIBSSH2_ALLOC(session,
                                 strlen(session->server_sign_algorithms) + 1);"
    )
    string(FIND "${original}" "${replacement}" already_patched)
    if(already_patched EQUAL -1)
        string(FIND "${original}" "${anchor}" position)
        if(position EQUAL -1)
            message(
                FATAL_ERROR
                    "Pinned libssh2 signature-list patch no longer matches; review dependency source"
            )
        endif()
        string(REPLACE "${anchor}" "${replacement}" content "${original}")
        file(WRITE "${path}" "${content}")
    endif()

    if(WIN32)
        # Backport libssh2 f216bbe: fixed-width DH values need canonical mpint form.
        set(path "${source_dir}/src/wincng.c")
        file(READ "${path}" original)
        set(content "${original}")
        set(anchor "void
_libssh2_dh_init(_libssh2_dh_ctx *dhctx)"
        )
        set(replacement
            "/* CommunicationDriver: normalize WinCNG DH public values and secrets. */
static void
_libssh2_wincng_bignum_normalize(_libssh2_bn *bn)
{
    ULONG offset = 0;
    if(!bn || !bn->bignum || !bn->length)
        return;
    while(!bn->bignum[offset] && offset + 1 < bn->length)
        offset++;
    if(offset > 0) {
        memmove(bn->bignum, bn->bignum + offset, bn->length - offset);
        _libssh2_explicit_zero(bn->bignum + bn->length - offset, offset);
        bn->length -= offset;
    }
}

${anchor}"
        )
        cd_patch_libssh2_fragment(content "${anchor}" "${replacement}")
        set(anchor
            "        memcpy(public->bignum, (unsigned char *)dh_key_blob +
                               sizeof(*dh_key_blob) +
                               2 * dh_key_blob->cbKey,
               dh_key_blob->cbKey);"
        )
        cd_patch_libssh2_fragment(
            content "${anchor}" "${anchor}
        _libssh2_wincng_bignum_normalize(public);"
        )
        set(anchor "    if(_libssh2_wincng_bignum_mod_exp(public, g, dhctx->dh_privbn, p))
        return -1;"
        )
        cd_patch_libssh2_fragment(
            content "${anchor}" "${anchor}
    _libssh2_wincng_bignum_normalize(public);"
        )
        set(anchor "        status = 0;
        _libssh2_wincng.hasAlgDHwithKDF = 1;"
        )
        cd_patch_libssh2_fragment(
            content "${anchor}" "        _libssh2_wincng_bignum_normalize(secret);
${anchor}"
        )
        set(anchor "    return _libssh2_wincng_bignum_mod_exp(secret, f, dhctx->dh_privbn, p);")
        cd_patch_libssh2_fragment(
            content
            "${anchor}"
            "    if(_libssh2_wincng_bignum_mod_exp(secret, f, dhctx->dh_privbn, p))
        return -1;
    _libssh2_wincng_bignum_normalize(secret);
    return 0;"
        )
        if(NOT content STREQUAL original)
            file(WRITE "${path}" "${content}")
        endif()
    endif()
endfunction()
