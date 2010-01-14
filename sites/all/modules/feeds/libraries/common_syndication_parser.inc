<?php
// $Id: common_syndication_parser.inc,v 1.4 2009/12/21 00:16:22 alexb Exp $

/**
 * @file
 *   Downloading and parsing functions for Common Syndication Parser.
 *   Pillaged from FeedAPI common syndication parser.
 *
 * @todo Restructure. OO could work wonders here.
 * @todo Write unit tests.
 * @todo Keep in Feeds project or host on Drupal?
 */

/**
 * Parse the feed into a data structure.
 *
 * @param $feed
 *  The feed object (contains the URL or the parsed XML structure.
 * @return
 *  stdClass The structured datas extracted from the feed.
 */
function common_syndication_parser_parse($string) {
  if (!defined('LIBXML_VERSION') || (version_compare(phpversion(), '5.1.0', '<'))) {
    @ $xml = simplexml_load_string($string, NULL);
  }
  else {
    @ $xml = simplexml_load_string($string, NULL, LIBXML_NOERROR | LIBXML_NOWARNING | LIBXML_NOCDATA);
  }

  // Got a malformed XML.
  if ($xml === FALSE || is_null($xml)) {
    return FALSE;
  }
  $feed_type = _parser_common_syndication_feed_format_detect($xml);
  if ($feed_type ==  "atom1.0") {
    return _parser_common_syndication_atom10_parse($xml);
  }
  if ($feed_type == "RSS2.0" || $feed_type == "RSS0.91" || $feed_type == "RSS0.92") {
    return _parser_common_syndication_RSS20_parse($xml);
  }
  if ($feed_type == "RDF") {
    return _parser_common_syndication_RDF10_parse($xml);
  }
  return FALSE;
}

/**
 * Get the cached version of the <var>$url</var>
 */
function _parser_common_syndication_cache_get($url) {
  $cache_file = _parser_common_syndication_sanitize_cache() .'/'. md5($url);
  if (file_exists($cache_file)) {
    $file_content = file_get_contents($cache_file);
    return unserialize($file_content);
  }
  return FALSE;
}

/**
 * Determine the feed format of a SimpleXML parsed object structure.
 *
 * @param $xml
 *  SimpleXML-preprocessed feed.
 * @return
 *  The feed format short description or FALSE if not compatible.
 */
function _parser_common_syndication_feed_format_detect($xml) {
  if (!is_object($xml)) {
    return FALSE;
  }
  $attr = $xml->attributes();
  $type = strtolower($xml->getName());
  if (isset($xml->entry) && $type == "feed") {
    return "atom1.0";
  }
  if ($type == "rss" && $attr["version"] == "2.0") {
    return "RSS2.0";
  }
  if ($type == "rdf" && isset($xml->channel)) {
    return "RDF";
  }
  if ($type == "rss" && $attr["version"] == "0.91") {
    return "RSS0.91";
  }
  if ($type == "rss" && $attr["version"] == "0.92") {
    return "RSS0.92";
  }
  return FALSE;
}

/**
 * Parse atom feeds.
 */
function _parser_common_syndication_atom10_parse($feed_XML) {
  $parsed_source = array();

  $base = (string) array_shift($feed_XML->xpath("@base"));
  if (!valid_url($base, TRUE)) {
    $base = FALSE;
  }

  // Detect the title
  $parsed_source['title'] = isset($feed_XML->title) ? _parser_common_syndication_title("{$feed_XML->title}") : "";
  // Detect the description
  $parsed_source['description'] = isset($feed_XML->subtitle) ? "{$feed_XML->subtitle}" : "";

  $parsed_source['link'] = _parser_common_syndication_link($feed_XML->link);
  if (valid_url($parsed_source['link']) && !valid_url($parsed_source['link'], TRUE) && !empty($base)) {
    $parsed_source['link'] = $base . $parsed_source['link'];
  }

  $parsed_source['items'] = array();

  foreach ($feed_XML->entry as $news) {
    $original_url = NULL;

    $guid = !empty($news->id) ? "{$news->id}" : NULL;

    // I don't know how standard this is, but sometimes the id is the URL.
    if (valid_url($guid, TRUE)) {
      $original_url = $guid;
    }

    $additional_taxonomies = array();

    if (isset($news->category)) {
      $additional_taxonomies['ATOM Categories'] = array();
      $additional_taxonomies['ATOM Domains'] = array();
      foreach ($news->category as $category) {
        if (isset($category['scheme'])) {
          $domain = "{$category['scheme']}";
          if (!empty($domain)) {
              if (!isset($additional_taxonomies['ATOM Domains'][$domain])) {
                $additional_taxonomies['ATOM Domains'][$domain] = array();
              }
              $additional_taxonomies['ATOM Domains'][$domain][] = count($additional_taxonomies['ATOM Categories']) - 1;
          }
        }
        $additional_taxonomies['ATOM Categories'][] = "{$category['term']}";
      }
    }
    $title = "{$news->title}";

    $body = '';
    if (!empty($news->content)) {
      foreach ($news->content->children() as $child)  {
        $body .= $child->asXML();
      }
      $body .= "{$news->content}";
    }
    else if (!empty($news->summary)) {
      foreach ($news->summary->children() as $child)  {
        $body .= $child->asXML();
      }
      $body .= "{$news->summary}";
    }

    if (!empty($news->content['src'])) {
      // some src elements in some valid atom feeds contained no urls at all
      if (valid_url("{$news->content['src']}", TRUE)) {
        $original_url = "{$news->content['src']}";
      }
    }

    $author_found = FALSE;

    if (!empty($news->source->author->name)) {
      $original_author = "{$news->source->author->name}";
      $author_found = TRUE;
    }
    else if (!empty($news->author->name)) {
      $original_author = "{$news->author->name}";
      $author_found = TRUE;
    }

    if (!empty($feed_XML->author->name) && !$author_found) {
      $original_author = "{$feed_XML->author->name}";
    }

    $original_url = _parser_common_syndication_link($news->link);

    $item = array();
    $item['title'] = _parser_common_syndication_title($title, $body);
    $item['description'] = $body;
    $item['author'] = $original_author;
    $item['timestamp'] = _parser_common_syndication_parse_date(isset($news->published) ? "{$news->published}" : "{$news->issued}");
    $item['url'] = trim($original_url);
    if (valid_url($item['url']) && !valid_url($item['url'], TRUE) && !empty($base)) {
      $item['url'] = $base . $item['url'];
    }
    // Fall back on URL if GUID is empty.
    if (!empty($guid)) {
      $item['guid'] = $guid;
    }
    else {
      $item['guid'] = $item['url'];
    }
    $item['tags'] = isset($additional_taxonomies['ATOM Categories']) ? $additional_taxonomies['ATOM Categories'] : array();
    $item['domains'] = isset($additional_taxonomies['ATOM Domains']) ? $additional_taxonomies['ATOM Domains'] : array();
    $parsed_source['items'][] = $item;
  }
  return $parsed_source;
}

/**
 * Parse RDF Site Summary (RSS) 1.0 feeds in RDF/XML format.
 *
 * @see http://web.resource.org/rss/1.0/
 */
function _parser_common_syndication_RDF10_parse($feed_XML) {
  // Declare some canonical standard prefixes for well-known namespaces:
  static $canonical_namespaces = array(
    'rdf'      => 'http://www.w3.org/1999/02/22-rdf-syntax-ns#',
    'rdfs'     => 'http://www.w3.org/2000/01/rdf-schema#',
    'xsi'      => 'http://www.w3.org/2001/XMLSchema-instance#',
    'xsd'      => 'http://www.w3.org/2001/XMLSchema#',
    'owl'      => 'http://www.w3.org/2002/07/owl#',
    'dc'       => 'http://purl.org/dc/elements/1.1/',
    'dcterms'  => 'http://purl.org/dc/terms/',
    'dcmitype' => 'http://purl.org/dc/dcmitype/',
    'foaf'     => 'http://xmlns.com/foaf/0.1/',
    'rss'      => 'http://purl.org/rss/1.0/',
  );

  // Get all namespaces declared in the feed element, with special handling
  // for PHP versions prior to 5.1.2 as they don't handle namespaces.
  $namespaces = version_compare(phpversion(), '5.1.2', '<') ? array() : $feed_XML->getNamespaces(TRUE);

  // Process the <rss:channel> resource containing feed metadata:
  foreach ($feed_XML->children($canonical_namespaces['rss'])->channel as $rss_channel) {
    $parsed_source = array(
      'title'       => _parser_common_syndication_title((string)$rss_channel->title),
      'description' => (string)$rss_channel->description,
      'options'     => (object)array('link' => (string)$rss_channel->link),
      'items'       => array(),
    );
    break;
  }

  // Process each <rss:item> resource contained in the feed:
  foreach ($feed_XML->children($canonical_namespaces['rss'])->item as $rss_item) {

    // Extract all available RDF statements from the feed item's RDF/XML
    // tags, allowing for both the item's attributes and child elements to
    // contain RDF properties:
    $rdf_data = array();
    foreach ($namespaces as $ns => $ns_uri) {
      // Note that we attempt to normalize the found property name
      // namespaces to well-known 'standard' prefixes where possible, as the
      // feed may in principle use any arbitrary prefixes and we should
      // still be able to correctly handle it.
      foreach ($rss_item->attributes($ns_uri) as $attr_name => $attr_value) {
        $ns_prefix = ($ns_prefix = array_search($ns_uri, $canonical_namespaces)) ? $ns_prefix : $ns;
        $rdf_data[$ns_prefix .':'. $attr_name][] = (string)$attr_value;
      }
      foreach ($rss_item->children($ns_uri) as $rss_property) {
        $ns_prefix = ($ns_prefix = array_search($ns_uri, $canonical_namespaces)) ? $ns_prefix : $ns;
        $rdf_data[$ns_prefix .':'. $rss_property->getName()][] = (string)$rss_property;
      }
    }

    // Declaratively define mappings that determine how to construct the result object.
    $item = _parser_common_syndication_RDF10_item($rdf_data, (object)array(
      'title'       => array('rss:title', 'dc:title'),
      'description' => array('rss:description', 'dc:description', 'content:encoded'),
      'options'     => (object)array(
        'guid'            => 'rdf:about',
        'timestamp'       => 'dc:date',
        'author' => array('dc:creator', 'dc:publisher'),
        'url'    => array('rss:link', 'rdf:about'),
        'tags'            => 'dc:subject',
      ),
    ));

    // Special handling for the title:
    $item['title'] = _parser_common_syndication_title($item['title'], $item['description']);

    // Parse any date/time values into Unix timestamps:
    $item['timestamp'] = _parser_common_syndication_parse_date($item['timestamp']);

    // If no author name found, use the feed title:
    if (empty($item['author'])) {
      $item['author'] = $parsed_source['title'];
    }

    // If no GUID found, use the URL of the feed.
    if (empty($item['guid'])) {
      $item['guid'] = $item['url'];
    }

    // Add every found RDF property to the feed item.
    $item['rdf'] = (object)array();
    foreach ($rdf_data as $rdf_property => $rdf_value) {
      // looks nicer in the mapper UI
      // @todo Revisit, not used with feedapi mapper anymore.
      $rdf_property = str_replace(':', '_', $rdf_property);
      $item['rdf'][$rdf_property] = $rdf_value;
    }

    $parsed_source['items'][] = $item;
  }

  return $parsed_source;
}

function _parser_common_syndication_RDF10_property($rdf_data, $rdf_properties = array()) {
  $rdf_properties = is_array($rdf_properties) ? $rdf_properties : array_slice(func_get_args(), 1);
  foreach ($rdf_properties as $rdf_property) {
    if ($rdf_property && !empty($rdf_data[$rdf_property])) {
      // remove empty strings
      return array_filter($rdf_data[$rdf_property], 'strlen');
    }
  }
}

function _parser_common_syndication_RDF10_item($rdf_data, $mappings) {
  foreach (get_object_vars($mappings) as $k => $v) {
    if (is_object($v)) {
      $mappings->$k = _parser_common_syndication_RDF10_item($rdf_data, $v);
    }
    else {
      $values = _parser_common_syndication_RDF10_property($rdf_data, $v);
      $mappings->$k = !is_array($values) || count($values) > 1 ? $values : reset($values);
    }
  }
  return (object)$mappings;
}

/**
 * Parse RSS2.0 feeds.
 */
function _parser_common_syndication_RSS20_parse($feed_XML) {
  $parsed_source = array();
  // Detect the title.
  $parsed_source['title'] = isset($feed_XML->channel->title) ? _parser_common_syndication_title("{$feed_XML->channel->title}") : "";
  // Detect the description.
  $parsed_source['description'] = isset($feed_XML->channel->description) ? "{$feed_XML->channel->description}" : "";
  // Detect the link.
  $parsed_source['link'] = isset($feed_XML->channel->link) ? "{$feed_XML->channel->link}" : "";
  $parsed_source['items'] = array();

  foreach ($feed_XML->xpath('//item') as $news) {
    $category = $news->xpath('category');
    // Get children for current namespace.
    if (version_compare(phpversion(), '5.1.2', '>')) {
      $content = (array)$news->children('http://purl.org/rss/1.0/modules/content/');
    }
    $news = (array) $news;
    $news['category'] = $category;

    if (isset($news['title'])) {
      $title = "{$news['title']}";
    }
    else {
      $title = '';
    }

    if (isset($news['description'])) {
      $body = "{$news['description']}";
    }
    // Some sources use content:encoded as description i.e.
    // PostNuke PageSetter module.
    if (isset($news['encoded'])) {  // content:encoded for PHP < 5.1.2.
      if (strlen($body) < strlen("{$news['encoded']}")) {
        $body = "{$news['encoded']}";
      }
    }
    if (isset($content['encoded'])) { // content:encoded for PHP >= 5.1.2.
      if (strlen($body) < strlen("{$content['encoded']}")) {
        $body = "{$content['encoded']}";
      }
    }
    if (!isset($body)) {
      $body = "{$news['title']}";
    }

    if (!empty($news['author'])) {
      $original_author = "{$news['author']}";
    }

    if (!empty($news['link'])) {
      $original_url = "{$news['link']}";
    }
    else {
      $original_url = NULL;
    }

    if (isset($news['guid'])) {
      $guid = "{$news['guid']}";
    }
    else {
      // Attempt to fall back on original URL if GUID is not present.
      $guid = $original_url;
    }

    $additional_taxonomies = array();
    $additional_taxonomies['RSS Categories'] = array();
    $additional_taxonomies['RSS Domains'] = array();
    if (isset($news['category'])) {
      foreach ($news['category'] as $category) {
        $additional_taxonomies['RSS Categories'][] = "{$category}";
        if (isset($category['domain'])) {
          $domain = "{$category['domain']}";
          if (!empty($domain)) {
              if (!isset($additional_taxonomies['RSS Domains'][$domain])) {
                $additional_taxonomies['RSS Domains'][$domain] = array();
              }
              $additional_taxonomies['RSS Domains'][$domain][] = count($additional_taxonomies['RSS Categories']) - 1;
          }
        }
      }
    }

    $item = array();
    $item['title'] = _parser_common_syndication_title($title, $body);
    $item['description'] = $body;
    $item['author'] = $original_author;
    if (isset($news['pubDate'])) {
      $item['timestamp'] = _parser_common_syndication_parse_date($news['pubDate']);
    }
    else {
      $item['timestamp'] = time();
    }
    $item['url'] = trim($original_url);
    $item['guid'] = $guid;
    $item['domains'] = $additional_taxonomies['RSS Domains'];
    $item['tags'] = $additional_taxonomies['RSS Categories'];
    $parsed_source['items'][] = $item;
  }
  return $parsed_source;
}

/**
 * Parse a date comes from a feed.
 *
 * @param $date_string
 *  The date string in various formats.
 * @return
 *  The timestamp of the string or the current time if can't be parsed
 */
function _parser_common_syndication_parse_date($date_str) {
  $parsed_date = strtotime($date_str);
  if ($parsed_date === FALSE || $parsed_date == -1) {
    $parsed_date = _parser_common_syndication_parse_w3cdtf($date_str);
  }
  return $parsed_date === FALSE ? time() : $parsed_date;
}

/**
 * Parse the W3C date/time format, a subset of ISO 8601.
 *
 * PHP date parsing functions do not handle this format.
 * See http://www.w3.org/TR/NOTE-datetime for more information.
 * Originally from MagpieRSS (http://magpierss.sourceforge.net/).
 *
 * @param $date_str
 *   A string with a potentially W3C DTF date.
 * @return
 *   A timestamp if parsed successfully or FALSE if not.
 */
function _parser_common_syndication_parse_w3cdtf($date_str) {
  if (preg_match('/(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(:(\d{2}))?(?:([-+])(\d{2}):?(\d{2})|(Z))?/', $date_str, $match)) {
    list($year, $month, $day, $hours, $minutes, $seconds) = array($match[1], $match[2], $match[3], $match[4], $match[5], $match[6]);
    // Calculate the epoch for current date assuming GMT.
    $epoch = gmmktime($hours, $minutes, $seconds, $month, $day, $year);
    if ($match[10] != 'Z') { // Z is zulu time, aka GMT
      list($tz_mod, $tz_hour, $tz_min) = array($match[8], $match[9], $match[10]);
      // Zero out the variables.
      if (!$tz_hour) {
        $tz_hour = 0;
      }
      if (!$tz_min) {
        $tz_min = 0;
      }
      $offset_secs = (($tz_hour * 60) + $tz_min) * 60;
      // Is timezone ahead of GMT?  If yes, subtract offset.
      if ($tz_mod == '+') {
        $offset_secs *= -1;
      }
      $epoch += $offset_secs;
    }
    return $epoch;
  }
  else {
    return FALSE;
  }
}

/**
 * Extract the link that points to the original content (back to site or
 * original article)
 *
 * @param $links
 *  Array of SimpleXML objects
 */
function _parser_common_syndication_link($links) {
  $to_link = '';
  if (count($links) > 0) {
    foreach ($links as $link) {
      $link = $link->attributes();
      $to_link = isset($link["href"]) ? "{$link["href"]}" : "";
      if (isset($link["rel"])) {
        if ("{$link["rel"]}" == 'alternate') {
          break;
        }
      }
    }
  }
  return $to_link;
}

/**
 * Prepare raw data to be a title
 */
function _parser_common_syndication_title($title, $body = FALSE) {
  if (empty($title) && !empty($body)) {
    // Explode to words and use the first 3 words.
    $words = preg_split("/[\s,]+/", $body);
    $title = $words[0] .' '. $words[1] .' '. $words[2];
  }
  return $title;
}