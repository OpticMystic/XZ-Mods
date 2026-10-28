CREATE TABLE installations (
  installation_id TEXT PRIMARY KEY CHECK(length(installation_id)=11),
  key_hash TEXT NOT NULL CHECK(length(key_hash)=64),
  created_at INTEGER NOT NULL,
  day TEXT NOT NULL,
  ip_hash TEXT NOT NULL
);
CREATE INDEX installations_rate ON installations(day,ip_hash);
CREATE TRIGGER installation_limits BEFORE INSERT ON installations BEGIN
  SELECT CASE WHEN (SELECT count(*) FROM installations WHERE day=NEW.day AND ip_hash=NEW.ip_hash)>=10
    OR (SELECT count(*) FROM installations WHERE day=NEW.day)>=100 THEN RAISE(ABORT,'quota') END;
END;
CREATE TABLE loaders (
  loader_id TEXT PRIMARY KEY CHECK(length(loader_id)=11),
  installation_id TEXT NOT NULL REFERENCES installations(installation_id),
  key_hash TEXT NOT NULL CHECK(length(key_hash)=64),
  revoked INTEGER NOT NULL DEFAULT 0 CHECK(revoked IN (0,1)),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE INDEX loaders_installation ON loaders(installation_id);
CREATE TRIGGER loader_limits BEFORE INSERT ON loaders BEGIN
  SELECT CASE WHEN (SELECT count(*) FROM loaders WHERE installation_id=NEW.installation_id)>=64
    THEN RAISE(ABORT,'quota') END;
END;
ALTER TABLE reports ADD COLUMN origin TEXT NOT NULL DEFAULT 'loader' CHECK(origin IN ('loader','controller'));
ALTER TABLE reports ADD COLUMN installation_id TEXT;
ALTER TABLE reports ADD COLUMN loader_id TEXT;
ALTER TABLE reports ADD COLUMN loader_key_hash TEXT;
ALTER TABLE reports ADD COLUMN is_pending INTEGER NOT NULL DEFAULT 0 CHECK(is_pending IN (0,1));
UPDATE reports SET origin='controller' WHERE public_json IS NOT NULL AND json_valid(public_json) AND json_extract(public_json,'$.origin')='controller';
CREATE INDEX reports_installation_day ON reports(installation_id,day);
CREATE INDEX reports_loader_day ON reports(loader_id,day);
CREATE TRIGGER controller_limits BEFORE INSERT ON reports WHEN NEW.is_pending=1 BEGIN
  SELECT CASE WHEN NEW.installation_id IS NULL OR NOT EXISTS(SELECT 1 FROM installations WHERE installation_id=NEW.installation_id)
    THEN RAISE(ABORT,'queue_auth') END;
  SELECT CASE WHEN NEW.loader_id IS NOT NULL AND NOT EXISTS(SELECT 1 FROM loaders WHERE loader_id=NEW.loader_id AND installation_id=NEW.installation_id AND revoked=0)
    THEN RAISE(ABORT,'queue_auth') END;
  SELECT CASE WHEN NEW.loader_key_hash IS NOT NULL AND NOT EXISTS(SELECT 1 FROM loaders WHERE loader_id=NEW.loader_id AND installation_id=NEW.installation_id AND key_hash=NEW.loader_key_hash AND revoked=0)
    THEN RAISE(ABORT,'queue_auth') END;
  SELECT CASE WHEN (SELECT count(*) FROM reports WHERE day=NEW.day AND installation_id=NEW.installation_id)>=20
    OR (NEW.loader_id IS NOT NULL AND (SELECT count(*) FROM reports WHERE day=NEW.day AND loader_id=NEW.loader_id)>=5)
    THEN RAISE(ABORT,'quota') END;
END;
ALTER TABLE report_tracking ADD COLUMN origin TEXT NOT NULL DEFAULT 'loader' CHECK(origin IN ('loader','controller'));
ALTER TABLE report_tracking ADD COLUMN installation_id TEXT;
ALTER TABLE report_tracking ADD COLUMN loader_id TEXT;
ALTER TABLE report_tracking ADD COLUMN comments_state TEXT NOT NULL DEFAULT 'completed' CHECK(comments_state IN ('pending_comments','completed','completed_without_comments','expired_reminders'));
ALTER TABLE report_tracking ADD COLUMN reminders_until INTEGER;
ALTER TABLE report_tracking ADD COLUMN reminders_dismissed INTEGER NOT NULL DEFAULT 0 CHECK(reminders_dismissed IN (0,1));
UPDATE report_tracking SET origin=COALESCE((SELECT origin FROM reports WHERE id=report_id),'loader');
CREATE INDEX tracking_installation_queue ON report_tracking(installation_id,seq);
CREATE TABLE controller_queue (
  report_id TEXT PRIMARY KEY REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  description TEXT,
  topic TEXT,
  screen_state TEXT,
  claimed INTEGER NOT NULL DEFAULT 0 CHECK(claimed IN (0,1)),
  completion_hash TEXT,
  comments TEXT,
  completed_at INTEGER,
  private_expires_at INTEGER NOT NULL
);
CREATE TRIGGER controller_accept_auth BEFORE UPDATE OF state ON reports WHEN NEW.state='accepted' AND NEW.is_pending=1 AND NEW.loader_key_hash IS NOT NULL BEGIN
  SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM loaders WHERE loader_id=NEW.loader_id AND installation_id=NEW.installation_id AND key_hash=NEW.loader_key_hash AND revoked=0)
    THEN RAISE(ABORT,'queue_auth') END;
END;
CREATE TRIGGER controller_delete_private AFTER UPDATE OF state ON reports WHEN NEW.state IN ('deleting','gone') BEGIN
  UPDATE controller_queue SET description=NULL,topic=NULL,screen_state=NULL WHERE report_id=NEW.id;
END;
