<?php
/**
 * @file
 * Core Key Authentication functionality. Except queries, this is Drupal
 * independent.
 */

/**
 * Look up a private key.
 */
function keyauth_key($public_key) {
  return db_result(db_query("SELECT private_key FROM {keyauth_keys} WHERE public_key = '%s'", $public_key));
}

/**
 * Sign a message.
 *
 * @param $public_key
 *   The public key identifying a private key.
 * @param $message
 *   A string that is the message to hash.
 *
 * @return
 *   An array with the following keys:
 *   'nonce' - A random unique nonce.
 *   'timestamp' - The timestamp denoting the validity of the nonce.
 *   'hash' - The hash of message, nonce and timestamp.
 */
function keyauth_sign($public_key, $message) {
  $nonce = keyauth_random();
  $timestamp = time();
  $hash = hash_hmac(KEYAUTH_ALGORITHM, $message . $nonce . $timestamp, keyauth_key($public_key));
  return array(
    'nonce' => $nonce,
    'timestamp' => $timestamp,
    'hash' => $hash,
  );
}

/**
 * Verify a message.
 */
function keyauth_verify($public_key, $message, $nonce, $timestamp, $hash) {
  if ($private_key = keyauth_key($public_key)) {
    if (_keyauth_verify_nonce($public_key, $nonce, $timestamp)) {
      return $hash == hash_hmac(KEYAUTH_ALGORITHM, $message . $nonce . $timestamp, $private_key);
    }
  }
  return FALSE;
}

/**
 * Sign a URL.
 */
function keyauth_sign_url($public_key, $url) {
  // Normalize URL.
  if ($fragment = parse_url($url, PHP_URL_FRAGMENT)) {
    $url = str_replace("#$fragment", '', $url);
  }
  $url = trim($url, '?&');
  // Sign.
  list($nonce, $timestamp, $hash) = array_values(keyauth_sign($public_key, $url));
  // Assemble.
  $delimiter = parse_url($url, PHP_URL_QUERY) ? '&' : '?';
  $url .= $delimiter . 'hash='. urlencode($hash) .'&key='. urlencode($public_key) .'&nonce='. urlencode($nonce) .'&timestamp='. $timestamp;
  if ($fragment) {
    $url .= "#$fragment";
  }
  return $url;
}

/**
 * Verify a URL.
 */
function keyauth_verify_url($public_key, $url) {
  // Normalize URL.
  if ($fragment = parse_url($url, PHP_URL_FRAGMENT)) {
    $url = str_replace("#$fragment", '', $url);
  }
  // Parse URL.
  $parsed = parse_url($url);
  if (!isset($parsed['query'])) {
    return FALSE;
  }
  parse_str($parsed['query'], $query);
  if (isset($query['hash']) && isset($query['key']) && isset($query['nonce']) && isset($query['timestamp'])) {
    $url = str_replace("hash={$query['hash']}&key={$query['key']}&nonce={$query['nonce']}&timestamp={$query['timestamp']}", '', $url);
    $hash = urldecode($query['hash']);
    $key = urldecode($query['key']);
    $nonce = urldecode($query['nonce']);
    $timestamp = $query['timestamp'];
  }
  else {
    return FALSE;
  }
  $url = trim($url, '?&');
  // Verify.
  return keyauth_verify($key, $url, $nonce, $timestamp, $hash);
}


/**
 * Privide a random, time dependent string.
 */
function keyauth_random($algorithm = 'md5') {
  return hash($algorithm, uniqid(rand(), true));
}

/**
 * Verify nonce.
 */
function _keyauth_verify_nonce($public_key, $nonce, $timestamp) {
  if (db_result(db_query("SELECT nonce FROM {keyauth_timestamp_nonce} WHERE public_key = '%s' AND nonce = '%s'", $public_key, $nonce))) {
    return FALSE;
  }
  $time = time();
  if ($timestamp > $time + KEYAUTH_EXPIRY || $timestamp < $time - KEYAUTH_EXPIRY) {
    return FALSE;
  }
  $save = array(
    'timestamp' => time(),
    'public_key' => $public_key,
    'nonce' => $nonce,
  );
  drupal_write_record('keyauth_timestamp_nonce', $save);
  return TRUE;
}