#ifndef __SCENARIOS_H__
#define __SCENARIOS_H__

#include <gnumeric.h>
#include <glib-object.h>

/* ------------------------------------------------------------------------- */

typedef struct {
	GnmDepManaged dep;
	GnmValue *value;
} GnmScenarioItem;

GType gnm_scenario_item_get_type (void);
GnmScenarioItem *gnm_scenario_item_new (Sheet *sheet);
void gnm_scenario_item_free (GnmScenarioItem *sci);
void gnm_scenario_item_set_range (GnmScenarioItem *sci,
				  const GnmSheetRange *sr);
void gnm_scenario_item_set_value (GnmScenarioItem *sci,
				  const GnmValue *v);
gboolean gnm_scenario_item_valid (const GnmScenarioItem *sci,
				  GnmSheetRange *sr);

/* ------------------------------------------------------------------------- */

#define GNM_SCENARIO_TYPE        (gnm_scenario_get_type ())
#define GNM_SCENARIO(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SCENARIO_TYPE, GnmScenario))
#define GNM_SCENARIO_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SCENARIO_TYPE, GnmScenarioClass))
#define GNM_IS_SCENARIO(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SCENARIO_TYPE))

struct GnmScenario_ {
	GObject parent;

        Sheet *sheet;
        char *name;
        char *comment;

	GSList *items;
};

typedef struct {
	GObjectClass parent_class;
} GnmScenarioClass;

GType gnm_scenario_get_type (void);

GnmScenario *gnm_scenario_new (char const *name, Sheet *sheet);

GnmScenario *gnm_scenario_dup (GnmScenario *sc, Sheet *new_sheet);

void gnm_scenario_set_comment (GnmScenario *sc, const char *comment);

void gnm_scenario_add_area (GnmScenario *sc, const GnmSheetRange *sr);

GOUndo *gnm_scenario_apply (GnmScenario *sc);

char *gnm_scenario_get_range_str (const GnmScenario *sc);

/* ------------------------------------------------------------------------- */

#endif
