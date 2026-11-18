-- Reference backfill: first 40 bits of report_id, sorted by report_id, then
-- linear probe +1 modulo 2^40. This is reproducible, including prefix collisions.
-- Identifiers have no authentication authority. Keep these allocation ledgers
-- after diagnostics/tracking cleanup to prevent reuse.
CREATE TABLE report_references (
  report_id TEXT PRIMARY KEY,
  report_reference TEXT NOT NULL UNIQUE CHECK(length(report_reference)=11 AND
    substr(report_reference,1,2)='R-' AND substr(report_reference,7,1)='-' AND
    (substr(report_reference,3,4) || substr(report_reference,8,4)) NOT GLOB '*[^0123456789ABCDEFGHJKMNPQRSTVWXYZ]*')
);
ALTER TABLE report_tracking ADD COLUMN report_reference TEXT;
CREATE UNIQUE INDEX tracking_report_reference ON report_tracking(report_reference);
CREATE TABLE _reference_backfill (report_id TEXT PRIMARY KEY, seed INTEGER NOT NULL);
CREATE TRIGGER reference_backfill AFTER INSERT ON _reference_backfill BEGIN
  INSERT INTO report_references(report_id,report_reference)
    SELECT NEW.report_id,'R-' || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 35) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 30) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 25) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 20) & 31)+1,1) || '-' || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 15) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 10) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 5) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 0) & 31)+1,1) FROM (
      WITH RECURSIVE probe(n) AS (
        SELECT NEW.seed UNION ALL
        SELECT (n+1) & 1099511627775 FROM probe
        WHERE EXISTS(SELECT 1 FROM report_references WHERE report_reference=('R-' || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 35) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 30) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 25) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 20) & 31)+1,1) || '-' || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 15) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 10) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 5) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 0) & 31)+1,1)))
      ) SELECT n FROM probe WHERE NOT EXISTS(
        SELECT 1 FROM report_references WHERE report_reference=('R-' || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 35) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 30) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 25) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 20) & 31)+1,1) || '-' || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 15) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 10) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 5) & 31)+1,1) || substr('0123456789ABCDEFGHJKMNPQRSTVWXYZ',((n >> 0) & 31)+1,1))) LIMIT 1
    );
END;
INSERT INTO _reference_backfill(report_id,seed)
  SELECT report_id,((instr('0123456789abcdef',substr(report_id,1,1))-1) << 36) + ((instr('0123456789abcdef',substr(report_id,2,1))-1) << 32) + ((instr('0123456789abcdef',substr(report_id,3,1))-1) << 28) + ((instr('0123456789abcdef',substr(report_id,4,1))-1) << 24) + ((instr('0123456789abcdef',substr(report_id,5,1))-1) << 20) + ((instr('0123456789abcdef',substr(report_id,6,1))-1) << 16) + ((instr('0123456789abcdef',substr(report_id,7,1))-1) << 12) + ((instr('0123456789abcdef',substr(report_id,8,1))-1) << 8) + ((instr('0123456789abcdef',substr(report_id,9,1))-1) << 4) + ((instr('0123456789abcdef',substr(report_id,10,1))-1) << 0) FROM report_tracking ORDER BY report_id;
UPDATE report_tracking SET report_reference=(SELECT report_reference FROM report_references r WHERE r.report_id=report_tracking.report_id);
DROP TABLE _reference_backfill;
CREATE TRIGGER reference_required BEFORE INSERT ON report_tracking BEGIN
  SELECT CASE WHEN NEW.report_reference IS NULL THEN RAISE(ABORT,'report_reference_required') END;
  SELECT CASE WHEN EXISTS(SELECT 1 FROM report_references WHERE report_id=NEW.report_id AND report_reference!=NEW.report_reference)
    THEN RAISE(ABORT,'report_reference_immutable') END;
END;
CREATE TRIGGER reference_registered AFTER INSERT ON report_tracking BEGIN
  INSERT INTO report_references(report_id,report_reference) VALUES(NEW.report_id,NEW.report_reference) ON CONFLICT(report_id) DO NOTHING;
END;
CREATE TRIGGER reference_immutable BEFORE UPDATE OF report_reference ON report_tracking WHEN NEW.report_reference IS NOT OLD.report_reference BEGIN
  SELECT RAISE(ABORT,'report_reference_immutable');
END;
CREATE TRIGGER reference_ledger_immutable BEFORE UPDATE ON report_references BEGIN
  SELECT RAISE(ABORT,'report_reference_immutable');
END;

CREATE TABLE support_items (
  item_number INTEGER PRIMARY KEY AUTOINCREMENT CHECK(item_number BETWEEN 1001 AND 9007199254740991),
  group_id TEXT NOT NULL UNIQUE
);
INSERT INTO sqlite_sequence(name,seq) VALUES('support_items',1000);
ALTER TABLE bug_groups ADD COLUMN item_number INTEGER REFERENCES support_items(item_number);
CREATE UNIQUE INDEX group_item_number ON bug_groups(item_number);
INSERT INTO support_items(group_id) SELECT id FROM bug_groups ORDER BY created_at,id;
UPDATE bug_groups SET item_number=(SELECT item_number FROM support_items WHERE group_id=bug_groups.id);
CREATE TRIGGER group_item_allocated AFTER INSERT ON bug_groups BEGIN
  INSERT INTO support_items(group_id) VALUES(NEW.id) ON CONFLICT(group_id) DO NOTHING;
  UPDATE bug_groups SET item_number=(SELECT item_number FROM support_items WHERE group_id=NEW.id) WHERE id=NEW.id;
END;
CREATE TRIGGER group_item_immutable BEFORE UPDATE OF item_number ON bug_groups WHEN OLD.item_number IS NOT NULL AND NEW.item_number IS NOT OLD.item_number BEGIN
  SELECT RAISE(ABORT,'item_number_immutable');
END;
CREATE TRIGGER item_ledger_immutable BEFORE UPDATE ON support_items BEGIN
  SELECT RAISE(ABORT,'item_number_immutable');
END;
CREATE TABLE report_item_history (
  report_id TEXT NOT NULL REFERENCES report_tracking(report_id) ON DELETE CASCADE,
  revision INTEGER NOT NULL,
  previous_item_number INTEGER,
  item_number INTEGER,
  created_at INTEGER NOT NULL,
  PRIMARY KEY(report_id,revision)
);
INSERT INTO report_item_history(report_id,revision,previous_item_number,item_number,created_at)
  SELECT m.report_id,m.triage_revision,NULL,g.item_number,m.joined_at FROM report_group_members m JOIN bug_groups g ON g.id=m.group_id;
CREATE INDEX group_members_item_lookup ON report_group_members(group_id,report_id);
