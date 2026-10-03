# Squire Audit Logging

<!-- [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending] -->

Squire Core owns audit logging.

Module lifecycle actions must emit structured audit records through Core. A module must not maintain a private lifecycle log that bypasses Core audit history.

## Required Module Lifecycle Events

The following events are reserved for module lifecycle auditing:

- `MODULE_DISCOVERED`
- `MODULE_QUALIFICATION_BEGIN`
- `MODULE_QUALIFICATION_PASS`
- `MODULE_QUALIFICATION_FAIL`
- `MODULE_LOAD_BEGIN`
- `MODULE_LOAD_SUCCESS`
- `MODULE_LOAD_FAIL`
- `MODULE_HOTLOAD_BEGIN`
- `MODULE_HOTLOAD_SUCCESS`
- `MODULE_HOTLOAD_FAIL`
- `MODULE_UNLOAD_BEGIN`
- `MODULE_UNLOAD_SUCCESS`
- `MODULE_UNLOAD_FAIL`
- `MODULE_UPDATE_BEGIN`
- `MODULE_UPDATE_SUCCESS`
- `MODULE_UPDATE_FAIL`
- `MODULE_REJECTED`
- `MODULE_ROLLBACK_BEGIN`
- `MODULE_ROLLBACK_SUCCESS`
- `MODULE_ROLLBACK_FAIL`

## Required Fields

Each audit event records:

- event
- status
- subject
- version
- detail

`subject` identifies the module or Core component involved.

`version` identifies the exact implementation version known at the time of the event.

`detail` records the deterministic reason, result, or failure condition.

## Example

```text
2026-10-03T10:30:00-0700 [AUDIT] event="MODULE_HOTLOAD_SUCCESS" status="SUCCESS" subject="irc" version="0.1.0" detail="qualification passed; module activated"
```

## Audit Rules

1. Lifecycle changes must be logged before and after execution where applicable.
2. Failures must record the reason known to Core.
3. Rejected modules must be logged.
4. Qualification failures must be logged.
5. Module updates must identify the version being replaced and the version being activated in `detail` until dedicated old/new version fields are introduced.
6. A successful update must not be logged until the new module is active and qualified.
7. If rollback occurs, rollback actions and results must be logged separately from the failed update.
8. Module code must not be able to suppress Core audit events.
9. Audit messages must not contain untrusted line breaks or tabs that could forge additional log records.
10. Audit history is evidence of observed Core behavior; it is not proof that untested behavior works.
