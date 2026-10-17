# app/bandplan: Band plans in the interface

`bandplan_ui.cpp`: the plan picker with import / export / delete (top of the TV scan tab), the TV channel list in the toolbar and the scan ranges taken from the active plan. The plans themselves are in core (core/src/bandplan, docs/band-plans.md). The active plan is the preference `bandPlan` ("" automatic, "none" the built-in ranges, else a plan id).
