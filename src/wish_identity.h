/**
 * Copyright (C) 2018, ControlThings Oy Ab
 * Copyright (C) 2018, André Kaustell
 * Copyright (C) 2018, Jan Nyman
 * Copyright (C) 2018, Jepser Lökfors
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * @license Apache-2.0
 */
#pragma once

#include "stdbool.h"
#include "stdint.h"
#include "stddef.h"

#define WISH_ED25519_SEED_LEN   32

/** The maximum length of the Alias string of an identity, including terminating null char */
#define WISH_ALIAS_LEN 48

/* The length of a reference to an other identity - references are wuids
 * */
#define WISH_MAX_CONTACT_LEN WISH_ID_LEN
/* The maximum number of transports an identity can have */
#define WISH_MAX_TRANSPORTS 4
/* Maximum length of "transport URL" including the terminating null */
#define WISH_MAX_TRANSPORT_LEN 64

/** The maximum number of local identities (num id database entries in with privkeys) */
#define WISH_NUM_LOCAL_IDS 2

#define WISH_PUBKEY_LEN  32     /* Length of ED25519 pubkey */
#define WISH_PRIVKEY_LEN 64     /* Length of ED25519 privkey */
#define WISH_SIGNATURE_LEN 64   /* Length of ED25519 signature */
#define WISH_ID_LEN 32  /* Actually, length of a SH256 checksum */

/**
 * Wish in-memory Identity structure
 */
typedef struct {
    uint8_t uid[WISH_ID_LEN];
    uint8_t pubkey[WISH_PUBKEY_LEN];
    /** True, if the privkey buffer has a valid privkey */
    bool has_privkey;
    uint8_t privkey[WISH_PRIVKEY_LEN];
    char alias[WISH_ALIAS_LEN];
    char transports[WISH_MAX_TRANSPORTS][WISH_MAX_TRANSPORT_LEN];
    //char contacts[WISH_MAX_CONTACTS][WISH_MAX_CONTACT_LEN]; // wish-c99 currently does not limit how the identities see different contacts (as only one local identity is supported) 
    /** BSON object containing permissions (will be superseded by ACL) */
    const char* permissions;
    /** BSON object containing meta like phone, email etc */
    const char* meta;
} wish_identity_t;

#define WISH_ID_DB_NAME "wish_id_db.bson"

#include "wish_core.h"

/** This function returns the number of entries in the identity database (number of true identities + contacts),
 * Returns the number of identities, or -1 for errors */
int wish_get_num_uid_entries(void);

/**
 * This function returns the list of uids which are in the identity
 * database. A pointer to the list of uids are stored to the pointer given as
 * argument. 
 * Returns the number of uids in the list, or 0 if there are no
 * identities in the database, and a negative number for an error */
int wish_load_uid_list(wish_uid_list_elem_t *list, int list_len); 

/** 
 * Initializes the structure and loads the contact specified by 'uid', storing it to
 * the pointer 'contact'
 * 
 * Identity must be destroyed by wish_identity_destroy() independently of return value
 */
return_t wish_identity_load(const uint8_t *uid, wish_identity_t *identity);

/** 
 * Frees malloc'ed data from identity structure, but not the structure itself
 */
void wish_identity_destroy(wish_identity_t* identity);

// returns < 0 on error, == 0 is false, > 0 is true
int wish_identity_exists(uint8_t *uid);

/* This function load the identity specified by 'uid', and saves the
 * data in BSON format to identity_bson_doc */
int wish_load_identity_bson(uint8_t *uid, uint8_t *identity_bson_doc, size_t identity_bson_doc_max_len);

/* Save identity to database */
int wish_save_identity_entry(wish_identity_t *identity);

/* Save identity, expressed in BSON format, to the identity database */
int wish_save_identity_entry_bson(const uint8_t *identity_doc);

/** Get the the list of local identities, that is an array of id database entries which can be used for opening Wish connections, meaning that the privkey is also in the database.  
 * @param pointer to a caller-allocated list where result will be placed
 * @param length of the the caller-allocated list
 * @return number of local identities or 0 for an error
 */
int wish_get_local_identity_list(wish_uid_list_elem_t *list, int list_len);

/** Return true, if we have the privkey matching the uid 
 * (so that we can use the identity as our own identity) 
 */
int wish_has_privkey(uint8_t *uid);

/** Return true, if we have the pubkey matching the uid 
 * (so that we can connect to the system normally) 
 */
bool wish_has_pubkey(uint8_t *uid);


void wish_pubkey2uid(const uint8_t *pubkey, uint8_t *uid);

/* Create a new "local" identity (i.e. complete with pubkey and privkey)
 * with the alias provided, saving the result in the pointer 'id'.
 * The uid field is also populated */
void wish_create_local_identity(wish_core_t *core, wish_identity_t *id, const char *alias);

/**
 * This is a helper function for loading pubkeys corresponding to uids.
 * Easier to use than wish_load_identity
 *
 * Returns 0 when pubkey is found
 */
int wish_load_pubkey(uint8_t *uid, uint8_t *dst_buffer);

/*
 * Returns 0 when privkey is found
 */
int wish_load_privkey(uint8_t *uid, uint8_t *dst_buffer);


/**
 * Populate a struct wish_identity_t based on information in a 'cert'
 * which is obtained for example from a 'friend request'
 * 
 * @param new_id a pointer to the identity struct which will be populated
 * @param a pointer to BSON document from which the data will be read from
 * @return 0 for success
 */
int wish_identity_from_bson(wish_identity_t *id, const bson* bs);

/**
 * Add metadata, such as transports, to a wish_identity_t structure.
 * The metadata is expected have same structure as in a friend request "meta" element.
 * 
 * @param id the identity to add metadata to
 * @param meta The BSON bin buffer containing the metadata
 */
void wish_identity_add_meta_from_bson(wish_identity_t *id, const bson* meta);

/**
 * Update an identity from the database 
 *
 * @param uid the uid of the identity to be updated
 * @return returns 1 if the identity was updated, or 0 for none
 */
int wish_identity_update(wish_core_t* core, wish_identity_t* identity);

/**
 * Remove an identity from the database 
 *
 * @param uid the uid of the identity to be removed
 * @return returns 1 if the identity was removed, or 0 for none
 */
int wish_identity_remove(wish_core_t* core, uint8_t uid[WISH_ID_LEN]);

/** 
 * Remove the whole identity database 
 */
void wish_identity_delete_db(void);

return_t wish_identity_sign(wish_core_t* core, wish_identity_t* uid, const bin* data, const bin* claim, bin* signature);

return_t wish_identity_verify(wish_core_t* core, wish_identity_t* uid, const bin* data, const bin* claim, const bin* signature);

/**
 * Export identity by uid to bin buffer
 * 
 * @param core
 * @param id
 * @param buffer
 * @return 
 */
return_t wish_identity_export(wish_core_t *core, wish_identity_t *id, const char* signed_meta, bin *buffer);

return_t wish_build_signed_cert(wish_core_t *core, uint8_t *luid, const char* meta, bin *buffer);

/**
 * Returns bson_iterator from identity meta data from given fieldpath
 * 
 * Example 
 * 
 *     bson_iterator it = wish_identity_meta(&identity, "payment.BCH")
 * 
 *     bson_iterator_type(&it) == BSON_STRING
 *     const char* address = bson_iterator_string(&it);
 * 
 * @param identity
 * @param permission
 * @return bson_iterator
 */
bson_iterator wish_identity_meta(wish_identity_t* identity, const char* permission);

/**
 * Returns bson_iterator from identity permissions data from given fieldpath
 * 
 * Example 
 * 
 *     bson_iterator it = wish_identity_permissions(&identity, "payment.BCH")
 * 
 *     bson_iterator_type(&it) == BSON_STRING
 *     const char* address = bson_iterator_string(&it);
 * 
 * @param identity
 * @param permission
 * @return bson_iterator
 */
bson_iterator wish_identity_permissions(wish_identity_t* identity, const char* permission);

/**
 * Returns true, if the identity is banned from connecting, ie. it has permissions: { banned: true }
 * If property does not exist, then return false ("not banned").
 * 
 * @param identity
 * @return true, if explicitly banned, else false
 */
bool wish_identity_is_banned(wish_identity_t *identity);

bool wish_identity_has_meta_unconfirmed_friend_request_flag(wish_identity_t *identity);

/**
 * Returns false, if the identity should not be contacted, ie. it has meta: { connect: false }
 * If property does not exist, then return true ("can be connected to").
 * @param identity
 * @return false if we have flagged the identity as "do not connect".
 */
bool wish_identity_get_meta_connect(wish_identity_t *identity);

/**
 * Add property connect: <status> to identity.meta
 * @param id
 * @param status
 */
void wish_identity_add_meta_connect(wish_core_t *core, uint8_t *uid, bool status);

/**
 * Remote property connect: <bool>
 * @param id
 */
void wish_identity_remove_meta_connect(wish_core_t *core, uint8_t *uid);

void wish_identity_add_meta_unconfirmed_friend_request(wish_core_t *core, uint8_t *uid);

void wish_identity_remove_meta_unconfirmed_friend_request(wish_core_t *core, uint8_t *uid);