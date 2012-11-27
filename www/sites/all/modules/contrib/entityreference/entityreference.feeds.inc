<?php

/**
 * @file
 * Feeds mapping implementation for the Entity reference module
 */

/**
 * Implements hook_feeds_parser_sources_alter().
 */
function entityreference_feeds_parser_sources_alter(&$sources, $content_type) {
  if (!empty($content_type)) {
    foreach (field_info_fields() as $field) {
      if(in_array($content_type, $field['bundles']['node']) && $field['type'] == 'entityreference') {
        $sources['parent:entityreference:' . $field['field_name']] = array(
          'name' => t('Feed node: Entity reference: @field_name', array('@field_name' => $field['field_name'])),
          'description' => t('Entity reference from feed node.'),
          'callback' => 'entityreference_feeds_get_source',
        );
      }
    }
  }
}

/**
 * Callback, returns references from feed node.
 */
function entityreference_feeds_get_source(FeedsSource $source, FeedsParserResult $result, $key) {
  if ($node = node_load($source->feed_nid)) {
    $field = str_replace('parent:entityreference:', '', $key);
    foreach($node->{$field}['und'] as $reference) {
      $references[] = $reference['target_id'];
    }
    return $references;
  }
}

/**
 * Implements hook_feeds_processor_targets_alter().
 *
 * @see FeedsNodeProcessor::getMappingTargets().
 */
function entityreference_feeds_processor_targets_alter(&$targets, $entity_type, $bundle_name) {

  foreach (field_info_instances($entity_type, $bundle_name) as $name => $instance) {
    $info = field_info_field($name);
    if ($info['type'] == 'entityreference') {
      $targets[$name] = array(
        'name'        => check_plain($instance['label'] . t(' (Entity reference by Feeds GUID)')),
        'callback'    => 'entityreference_feeds_set_target',
        'description' => t('The field instance @label of @id matched by Feeds GUID.', array(
          '@label' => $instance['label'],
          '@id'    => $name,
        )),
      );
      $targets[$name . ':url'] = array(
        'name'        => check_plain($instance['label'] . t(' (Entity reference by Feeds URL)')),
        'callback'    => 'entityreference_feeds_set_target',
        'description' => t('The field instance @label of @id matched by Feeds URL.', array(
          '@label' => $instance['label'],
          '@id'    => $name,
        )),
        'real_target' => $name,
      );
      $targets[$name . ':etid'] = array(
        'name'        => check_plain($instance['label'] . t(' (Entity reference by Entity ID)')),
        'callback'    => 'entityreference_feeds_set_target',
        'description' => t('The field instance @label of @id matched by Entity ID.', array(
          '@label' => $instance['label'],
          '@id'    => $name,
        )),
        'real_target' => $name,
      );
      $targets[$name . ':label'] = array(
        'name'        => check_plain($instance['label'] . t(' (Entity reference by Entity label)')),
        'callback'    => 'entityreference_feeds_set_target',
        'description' => t('The field instance @label of @id matched by Entity label.', array(
          '@label' => $instance['label'],
          '@id'    => $name,
        )),
        'real_target' => $name,
      );
    }
  }
}

/**
 * Entity reference callback for mapping.
 *
 * When the callback is invoked, $target contains the name of the field the
 * user has decided to map to and $value contains the value of the feed item
 * element the user has picked as a source.
 *
 * @param $source
 *   A FeedsSource object.
 * @param $entity
 *   The entity to map to.
 * @param $target
 *   The target key on $entity to map to.
 * @param $value
 *   The value to map. MUST be an array.
 * @param $mapping
 *   Array of mapping settings for current value.
 * @param $input_format
 *   TRUE if an input format should be applied.
 */
function entityreference_feeds_set_target($source, $entity, $target, $value) {

  // Don't do anything if we weren't given any data.
  if (empty($value)) {
    return;
  }

  // Assume that the passed in value could really be any number of values.
  if (is_array($value)) {
    $values = $value;
  }
  else {
    $values = array($value);
  }

  // Determine the field we are matching against.
  if (strpos($target, ':') === false) {
    $match_key = 'guid';
  }
  else {
    list($target, $match_key) = explode(':', $target, 2);
  }

  // Get some useful field information.
  $info = field_info_field($target);
  if ($match_key == 'label') {
    $handler = entityreference_get_selection_handler($info);
  }

  // Set the language of the field depending on the mapping.
  $language = isset($mapping['language']) ? $mapping['language'] : LANGUAGE_NONE;

  // Iterate over all values.
  $iterator = 0;
  $field = isset($entity->$target) ? $entity->$target : array();
  foreach ($values as $value) {

    // Only process if this value was set for this instance.
    if ($value) {

      switch ($match_key) {
        case 'guid':
        case 'url':
          // Fetch the entity ID resulting from the mapping table look-up.
          $entity_id = db_select('feeds_item', 'fi')
            ->fields('fi', array('entity_id'))
            ->condition($match_key, $value,'=')
            ->execute()
            ->fetchField();
          break;
        case 'etid':
          $entity_id = $value;
          break;
        case 'label':
          $etids = array_keys($handler->getReferencableEntities($value, '='));
          // Use the first matching entity.
          $entity_id = reset($etids);
          break;
      }

      /*
       * Only add a reference to an existing entity ID if there exists a
       * mapping between it and the provided GUID.  In cases where no such
       * mapping exists (yet), don't do anything here.  There may be a mapping
       * defined later in the CSV file.  If so, and the user re-runs the import
       * (as a second pass), we can add this reference then.  (The "Update
       * existing nodes" option must be selected during the second pass.)
       */
      if ($entity_id) {

        // Assign the target ID.
        $field[$language][$iterator]['target_id']   = $entity_id;
      }
      else /* there is no $entity_id, no mapping */ {

        /*
         * Feeds stores a hash of every line imported from CSVs in order to
         * make the import process more efficient by ignoring lines it's
         * already seen.  We need to short-circuit this process in this case
         * because users may want to re-import the same line as an update later
         * when (and if) a map to a reference exists.  So in order to provide
         * this opportunity later, we need to destroy the hash.
         */
        unset($entity->feeds_item->hash);
        $source->log('entityreference', t('No existing entity found for entity @source_id entityreference to source entity @value', array('@source_id' => $entity->feeds_item->entity_id, '@value' => $value)));
      }
    }

    // Break out of the loop if this field is single-valued.
    if ($info['cardinality'] == 1) {
      break;
    }
    $iterator++;
  }

  // Add the field to the entity definition.
  $entity->{$target} = $field;
}