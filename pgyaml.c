#include <yaml.h>
#include <string.h>
#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/jsonb.h"
#include "utils/builtins.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

typedef struct JsonbInState
{
	JsonbParseState *parseState;
	JsonbValue *res;
} JsonbInState;

PG_FUNCTION_INFO_V1(yaml_to_jsonb);

static void merge_mapping(yaml_document_t *doc, yaml_node_t *target, yaml_node_t *source)
{
	yaml_node_pair_t *pair;

	if (!doc || !target || !source || target->type != YAML_MAPPING_NODE || source->type != YAML_MAPPING_NODE)
		return;

	pair = source->data.mapping.pairs.start;
	for (; pair < source->data.mapping.pairs.top; pair++)
	{
		int key_exists = 0;
		yaml_node_pair_t *target_pair = target->data.mapping.pairs.start;
		for (; target_pair < target->data.mapping.pairs.top; target_pair++)
		{
			yaml_node_t *target_key = yaml_document_get_node(doc, target_pair->key);
			yaml_node_t *source_key = yaml_document_get_node(doc, pair->key);
			
			if (target_key && source_key && 
				target_key->type == YAML_SCALAR_NODE && 
				source_key->type == YAML_SCALAR_NODE &&
				strcmp((const char *)target_key->data.scalar.value, 
					   (const char *)source_key->data.scalar.value) == 0)
			{
				key_exists = 1;
				break;
			}
		}

		if (!key_exists)
			yaml_document_append_mapping_pair(doc, target - doc->nodes.start + 1, pair->key, pair->value);
	}
}

static void process_mapping_merges(yaml_document_t *doc, yaml_node_t *map_node)
{
	int merge_count  = 0;
	int remove_count = 0;
	int merge_capacity  = 16;
	int remove_capacity = 16;
	yaml_node_t **merge_nodes;
	yaml_node_pair_t **remove_pairs;
	yaml_node_pair_t *pair;

	if (!doc || !map_node || map_node->type != YAML_MAPPING_NODE) return;

	merge_nodes  = malloc(sizeof(yaml_node_t *) * 16);
	remove_pairs = malloc(sizeof(yaml_node_pair_t *) * 16);
	if (!merge_nodes || !remove_pairs)
	{
		free(merge_nodes);
		free(remove_pairs);
		return;
	}

	pair = map_node->data.mapping.pairs.start;
	for (; pair < map_node->data.mapping.pairs.top; pair++)
	{
		yaml_node_t *key_node = yaml_document_get_node(doc, pair->key);
		if (key_node && key_node->type == YAML_SCALAR_NODE)
		{
			if (strcmp((const char *)key_node->data.scalar.value, "<<") == 0)
			{
				yaml_node_t *value_node = yaml_document_get_node(doc, pair->value);
				if (value_node)
				{
					if (value_node->type == YAML_SEQUENCE_NODE)
					{
						yaml_node_item_t *item = value_node->data.sequence.items.start;
						for (; item < value_node->data.sequence.items.top; item++)
						{
							yaml_node_t *merge_node = yaml_document_get_node(doc, *item);
							if (merge_node && merge_node->type == YAML_MAPPING_NODE)
							{
								if (merge_count >= merge_capacity)
								{
									yaml_node_t **new_nodes;
									merge_capacity *= 2;
									new_nodes = realloc(merge_nodes, sizeof(yaml_node_t *) * merge_capacity);
									if (!new_nodes)
									{
										free(merge_nodes);
										free(remove_pairs);
										return;
									}
									merge_nodes = new_nodes;
								}
								merge_nodes[merge_count++] = merge_node;
							}
						}
					}
					else if (value_node->type == YAML_MAPPING_NODE)
					{
						if (merge_count >= merge_capacity)
						{
							yaml_node_t **new_nodes;
							merge_capacity *= 2;
							new_nodes = realloc(merge_nodes, sizeof(yaml_node_t *) * merge_capacity);
							if (!new_nodes)
							{
								free(merge_nodes);
								free(remove_pairs);
								return;
							}
							merge_nodes = new_nodes;
						}
						merge_nodes[merge_count++] = value_node;
					}
				}

				if (remove_count >= remove_capacity)
				{
					yaml_node_pair_t **new_pairs;
					remove_capacity *= 2;
					new_pairs = realloc(remove_pairs, sizeof(yaml_node_pair_t *) * remove_capacity);
					if (!new_pairs)
					{
						free(merge_nodes);
						free(remove_pairs);
						return;
					}
					remove_pairs = new_pairs;
				}
				remove_pairs[remove_count++] = pair;
			}
		}
	}

	for (int i = 0; i < merge_count; i++)
	{
		merge_mapping(doc, map_node, merge_nodes[i]);
	}

	if (remove_count > 0)
	{
		size_t new_size = map_node->data.mapping.pairs.top - map_node->data.mapping.pairs.start - remove_count;
		yaml_node_pair_t *new_pairs = malloc(sizeof(yaml_node_pair_t) * new_size);
		if (new_pairs)
		{
			int new_idx = 0;
			pair = map_node->data.mapping.pairs.start;
			for (; pair < map_node->data.mapping.pairs.top; pair++)
			{
				int is_remove = 0;
				for (int i = 0; i < remove_count; i++)
				{
					if (pair == remove_pairs[i])
					{
						is_remove = 1;
						break;
					}
				}
				if (!is_remove)
				{
					new_pairs[new_idx++] = *pair;
				}
			}
			free(map_node->data.mapping.pairs.start);
			map_node->data.mapping.pairs.start = new_pairs;
			map_node->data.mapping.pairs.top = new_pairs + new_idx;
			map_node->data.mapping.pairs.end = new_pairs + new_idx;
		}
	}

	free(merge_nodes);
	free(remove_pairs);
}

static void yaml_node_to_jsonb_internal(yaml_document_t *doc, yaml_node_t *node, JsonbInState *state)
{
	if (!doc || !node || !state)
		return;

	switch (node->type)
	{
		case YAML_SCALAR_NODE:
		{
			JsonbValue jb;
			char *value = (char *)node->data.scalar.value;

			char *endptr;
			strtod(value, &endptr);
			if (strlen(value) == 0)
			{
				jb.type = jbvNull;
			}
			else if (*endptr == '\0')
			{
				Datum numd = DirectFunctionCall3(numeric_in, CStringGetDatum(value), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1));
				jb.type = jbvNumeric;
				jb.val.numeric = DatumGetNumeric(numd);
			}
			else if (strcasecmp(value, "true") == 0)
			{
				jb.type = jbvBool;
				jb.val.boolean = true;
			}
			else if (strcasecmp(value, "false") == 0)
			{
				jb.type = jbvBool;
				jb.val.boolean = false;
			}
			else if (strcasecmp(value, "null") == 0)
			{
				jb.type = jbvNull;
			}
			else
			{
				jb.type = jbvString;
				jb.val.string.len = strlen(value);
				jb.val.string.val = pstrdup(value);
			}

			if (state->parseState == NULL)
			{
				JsonbValue va;
				va.type = jbvArray;
				va.val.array.rawScalar = true;
				va.val.array.nElems = 1;

				state->res = pushJsonbValue(&state->parseState, WJB_BEGIN_ARRAY, &va);
				state->res = pushJsonbValue(&state->parseState, WJB_ELEM, &jb);
				state->res = pushJsonbValue(&state->parseState, WJB_END_ARRAY, NULL);
			}
			else
			{
				JsonbValue *parent = &state->parseState->contVal;
				switch (parent->type)
				{
					case jbvArray:
						state->res = pushJsonbValue(&state->parseState, WJB_ELEM, &jb);
						break;
					case jbvObject:
						state->res = pushJsonbValue(&state->parseState, WJB_VALUE, &jb);
						break;
					default:
						elog(ERROR, "unexpected parent type for scalar value");
				}
			}
			break;
		}
		case YAML_SEQUENCE_NODE:
		{
			yaml_node_item_t *item;
			state->res = pushJsonbValue(&state->parseState, WJB_BEGIN_ARRAY, NULL);

			for (item = node->data.sequence.items.start; 
				 item < node->data.sequence.items.top; 
				 item++)
			{
				yaml_node_t *child = yaml_document_get_node(doc, *item);
				yaml_node_to_jsonb_internal(doc, child, state);
			}

			state->res = pushJsonbValue(&state->parseState, WJB_END_ARRAY, NULL);
			break;
		}

		case YAML_MAPPING_NODE:
		{
			yaml_node_pair_t *pair;
			process_mapping_merges(doc, node);
			state->res = pushJsonbValue(&state->parseState, WJB_BEGIN_OBJECT, NULL);

			for (pair = node->data.mapping.pairs.start;
				 pair < node->data.mapping.pairs.top;
				 pair++)
			{
				JsonbValue key;
				yaml_node_t *key_node = yaml_document_get_node(doc, pair->key);
				yaml_node_t *val_node = yaml_document_get_node(doc, pair->value);

				if (key_node->type != YAML_SCALAR_NODE)
					elog(ERROR, "yaml object key must be scalar");

				key.type = jbvString;
				key.val.string.len = strlen((char *)key_node->data.scalar.value);
				key.val.string.val = pstrdup((char *)key_node->data.scalar.value);
				state->res = pushJsonbValue(&state->parseState, WJB_KEY, &key);

				yaml_node_to_jsonb_internal(doc, val_node, state);
			}

			state->res = pushJsonbValue(&state->parseState, WJB_END_OBJECT, NULL);
			break;
		}

		default:
			elog(ERROR, "unsupported yaml node type: %d", node->type);
	}
}

Datum yaml_to_jsonb(PG_FUNCTION_ARGS)
{
	text *yaml_text = PG_GETARG_TEXT_P(0);
	char *yaml_cstr = text_to_cstring(yaml_text);
	int error = 0;
	yaml_parser_t   parser;
	yaml_document_t document;
	JsonbInState    state;
	memset(&state, 0, sizeof(JsonbInState));

	if (!yaml_parser_initialize(&parser))
	{
		elog(ERROR, "Failed to initialize YAML parser.");
		error = 1;
	}
	else
	{
		yaml_parser_set_input_string(&parser, (const unsigned char *)yaml_cstr, strlen(yaml_cstr));

		if (!yaml_parser_load(&parser, &document))
		{
			elog(ERROR, "YAML parse error: %s (line: %lu, column: %lu)", parser.problem, 
				(unsigned long)parser.problem_mark.line + 1, (unsigned long)parser.problem_mark.column + 1);
			error = 1;
		}
		else
		{
			yaml_node_t *root = yaml_document_get_root_node(&document);
			if (root)
				yaml_node_to_jsonb_internal(&document, root, &state);
			else
				error = 1;
			yaml_document_delete(&document);
		}
		yaml_parser_delete(&parser);
	}

	if (error)
		PG_RETURN_NULL();

	PG_RETURN_POINTER(JsonbValueToJsonb(state.res));
}
