ALTER TABLE reports ADD COLUMN protocol_version INTEGER NOT NULL DEFAULT 1 CHECK(protocol_version IN (1,2));
ALTER TABLE reports ADD COLUMN evidence_revision INTEGER NOT NULL DEFAULT 1 CHECK(evidence_revision > 0);
ALTER TABLE reports ADD COLUMN legacy_public_consent INTEGER NOT NULL DEFAULT 0 CHECK(legacy_public_consent IN (0,1));

-- No FK to reports: diagnostic tombstone deletion must not delete tracking.
CREATE TABLE report_tracking (
  seq INTEGER PRIMARY KEY AUTOINCREMENT,
  report_id TEXT NOT NULL UNIQUE CHECK(length(report_id)=32 AND report_id NOT GLOB '*[^a-f0-9]*'),
  receipt_key_hash TEXT NOT NULL CHECK(length(receipt_key_hash)=64),
  read_key_hash TEXT,
  reply_key_hash TEXT,
  capability_revision INTEGER NOT NULL DEFAULT 1,
  payload_sha256 TEXT NOT NULL CHECK(length(payload_sha256)=64),
  revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
  classified_kind TEXT CHECK(classified_kind IN ('REGRESSION','DEGRADATION','BUG','EXPECTATION','FEATURE','NOT_A_BUG','QUESTION')),
  expectation TEXT CHECK(expectation IN ('STOCK_XZ','XZ_CONSISTENCY','DJ_CONVENTION','XZ_MODS_PREVIOUS','PREFERENCE','NONE')),
  impact TEXT CHECK(impact IN ('ADDITIVE','RESTORES_EXPECTED','OPT_IN_CHANGE','CHANGES_DEFAULT','BREAKS')),
  severity TEXT CHECK(severity IN ('LIVE_SET_STOPPER','WORKAROUND_EXISTS','ANNOYANCE','COSMETIC')),
  taxonomy_json TEXT,
  priority INTEGER NOT NULL DEFAULT 6,
  severity_rank INTEGER NOT NULL DEFAULT 4,
  kind_hint TEXT CHECK(kind_hint IN ('REGRESSION','DEGRADATION','BUG','EXPECTATION','FEATURE','QUESTION')),
  outcome TEXT CHECK(outcome IN ('NEEDS_INFO','CANNOT_REPRODUCE','DUPLICATE','CLASSIFIED')),
  lifecycle TEXT NOT NULL DEFAULT 'received' CHECK(lifecycle IN ('received','triaged','needs_info','cannot_reproduce','duplicate','fix_in_progress','fix_ready_for_testing','in_test_build','released','closed')),
  status_text_key TEXT NOT NULL DEFAULT 'received',
  question_key TEXT CHECK(question_key IN ('decks_stopped','reproduce_again','app_version','more_detail')),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  retention_until INTEGER NOT NULL,
  diagnostics_expires_at INTEGER NOT NULL,
  last_operation TEXT NOT NULL,
  last_operation_hash TEXT NOT NULL
);
CREATE INDEX tracking_inbox ON report_tracking(created_at,report_id);
CREATE INDEX tracking_expiry ON report_tracking(retention_until);

CREATE TABLE status_events (
  seq INTEGER PRIMARY KEY AUTOINCREMENT,
  report_id TEXT NOT NULL REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  revision INTEGER NOT NULL,
  classified_kind TEXT CHECK(classified_kind IN ('REGRESSION','DEGRADATION','BUG','EXPECTATION','FEATURE','NOT_A_BUG','QUESTION')),
  outcome TEXT CHECK(outcome IN ('NEEDS_INFO','CANNOT_REPRODUCE','DUPLICATE','CLASSIFIED')),
  lifecycle TEXT NOT NULL,
  status_text_key TEXT NOT NULL,
  question_key TEXT,
  created_at INTEGER NOT NULL,
  operation_id TEXT NOT NULL,
  operation_hash TEXT NOT NULL,
  UNIQUE(report_id,revision),
  UNIQUE(report_id,operation_id)
);
CREATE TRIGGER tracking_received AFTER INSERT ON report_tracking BEGIN
  INSERT INTO status_events(report_id,revision,classified_kind,outcome,lifecycle,status_text_key,question_key,created_at,operation_id,operation_hash)
  VALUES(NEW.report_id,NEW.revision,NEW.classified_kind,NEW.outcome,NEW.lifecycle,NEW.status_text_key,NEW.question_key,NEW.updated_at,NEW.last_operation,NEW.last_operation_hash);
END;
CREATE TRIGGER tracking_status AFTER UPDATE OF revision ON report_tracking WHEN NEW.revision=OLD.revision+1 BEGIN
  INSERT INTO status_events(report_id,revision,classified_kind,outcome,lifecycle,status_text_key,question_key,created_at,operation_id,operation_hash)
  VALUES(NEW.report_id,NEW.revision,NEW.classified_kind,NEW.outcome,NEW.lifecycle,NEW.status_text_key,NEW.question_key,NEW.updated_at,NEW.last_operation,NEW.last_operation_hash);
END;

CREATE TABLE private_notes (
  report_id TEXT NOT NULL REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  operation_id TEXT NOT NULL,
  operation_hash TEXT NOT NULL,
  revision INTEGER NOT NULL,
  content TEXT NOT NULL CHECK(length(CAST(content AS BLOB))<=16384),
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  PRIMARY KEY(report_id,operation_id)
);
CREATE TABLE triage_events (
  report_id TEXT NOT NULL REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  revision INTEGER NOT NULL,
  previous_kind TEXT CHECK(previous_kind IN ('REGRESSION','DEGRADATION','BUG','EXPECTATION','FEATURE','NOT_A_BUG','QUESTION')),
  new_kind TEXT NOT NULL CHECK(new_kind IN ('REGRESSION','DEGRADATION','BUG','EXPECTATION','FEATURE','NOT_A_BUG','QUESTION')),
  outcome TEXT NOT NULL CHECK(outcome IN ('NEEDS_INFO','CANNOT_REPRODUCE','DUPLICATE','CLASSIFIED')),
  evidence_json TEXT NOT NULL CHECK(length(CAST(evidence_json AS BLOB))<=16384),
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  PRIMARY KEY(report_id,revision)
);
CREATE TABLE bug_groups (
  id TEXT PRIMARY KEY CHECK(length(id)=32),
  kind TEXT NOT NULL CHECK(kind IN ('REGRESSION','DEGRADATION','BUG','EXPECTATION','FEATURE','NOT_A_BUG','QUESTION')),
  component TEXT NOT NULL,
  revision INTEGER NOT NULL DEFAULT 1,
  created_at INTEGER NOT NULL
);
CREATE TABLE bug_fingerprints (
  fingerprint_version INTEGER NOT NULL,
  kind TEXT NOT NULL CHECK(kind IN ('REGRESSION','DEGRADATION','BUG','EXPECTATION','FEATURE','NOT_A_BUG','QUESTION')),
  exact_hash TEXT NOT NULL CHECK(length(exact_hash)=64),
  family_hash TEXT NOT NULL CHECK(length(family_hash)=64),
  group_id TEXT NOT NULL REFERENCES bug_groups(id),
  PRIMARY KEY(fingerprint_version,kind,exact_hash)
);
CREATE INDEX fingerprint_family ON bug_fingerprints(kind,family_hash);
CREATE TABLE report_group_members (
  report_id TEXT PRIMARY KEY REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  group_id TEXT NOT NULL REFERENCES bug_groups(id),
  triage_revision INTEGER NOT NULL,
  joined_at INTEGER NOT NULL,
  public_participation INTEGER NOT NULL DEFAULT 0 CHECK(public_participation=0)
);

-- A populated public_json is not retroactive publication consent.
CREATE TABLE report_signatures (
  report_id TEXT PRIMARY KEY REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  exact_hash TEXT NOT NULL CHECK(length(exact_hash)=64),
  family_hash TEXT NOT NULL CHECK(length(family_hash)=64),
  component TEXT NOT NULL
);
CREATE TABLE tracking_sessions (
  token_hash TEXT PRIMARY KEY,
  report_id TEXT NOT NULL REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  capability_revision INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);

INSERT INTO report_tracking(report_id,receipt_key_hash,payload_sha256,created_at,updated_at,retention_until,diagnostics_expires_at,last_operation,last_operation_hash)
SELECT id,key_hash,digest,accepted_at,accepted_at,accepted_at+31536000000,expires_at,'accepted',digest
FROM reports WHERE accepted_at IS NOT NULL AND expires_at IS NOT NULL;

CREATE TABLE taxonomy_events (
  report_id TEXT NOT NULL REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  revision INTEGER NOT NULL,
  previous_json TEXT,
  new_json TEXT NOT NULL,
  reason_code TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY(report_id,revision)
);
CREATE TABLE inbox_snapshots (
  id TEXT PRIMARY KEY,
  high_water INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);
CREATE TABLE inbox_snapshot_rows (
  snapshot_id TEXT NOT NULL REFERENCES inbox_snapshots(id) ON DELETE CASCADE,
  ordinal INTEGER NOT NULL,
  report_id TEXT NOT NULL REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  PRIMARY KEY(snapshot_id,ordinal)
);
