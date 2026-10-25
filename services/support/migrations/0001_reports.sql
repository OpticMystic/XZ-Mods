CREATE TABLE reports (
  id TEXT PRIMARY KEY CHECK(length(id)=32),
  digest TEXT NOT NULL CHECK(length(digest)=64),
  key_hash TEXT NOT NULL CHECK(length(key_hash)=64),
  object_key TEXT NOT NULL UNIQUE,
  ip_hash TEXT NOT NULL,
  day TEXT NOT NULL,
  bytes INTEGER NOT NULL CHECK(bytes BETWEEN 1 AND 524288),
  state TEXT NOT NULL CHECK(state IN ('reserved','accepted','deleting','gone')),
  reserved_at INTEGER NOT NULL,
  accepted_at INTEGER,
  expires_at INTEGER,
  gone_at INTEGER,
  last_swept INTEGER NOT NULL DEFAULT 0,
  public_json TEXT,
  issue_state TEXT NOT NULL DEFAULT 'pending' CHECK(issue_state IN ('pending','user_provided','verified')),
  issue_url TEXT
);
CREATE INDEX report_day_ip ON reports(day,ip_hash);
CREATE INDEX report_cleanup ON reports(state,reserved_at,expires_at,gone_at);
CREATE TRIGGER reserve_limits BEFORE INSERT ON reports BEGIN
  SELECT CASE WHEN (SELECT count(*) FROM reports WHERE day=NEW.day AND ip_hash=NEW.ip_hash)>=5
    THEN RAISE(ABORT,'quota') END;
  SELECT CASE WHEN (SELECT count(*) FROM reports WHERE day=NEW.day)>=100
    OR (SELECT coalesce(sum(bytes),0) FROM reports WHERE day=NEW.day)+NEW.bytes>52428800
    OR (SELECT coalesce(sum(bytes),0) FROM reports WHERE state!='gone')+NEW.bytes>1073741824
    THEN RAISE(ABORT,'quota') END;
END;
