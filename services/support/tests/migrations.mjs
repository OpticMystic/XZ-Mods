import { readFile } from 'node:fs/promises';

// Preserve every statement in a trigger body, including INSERT and UPDATE.
export async function applyMigration(db, name) {
  let statement = '';
  for (const line of (await readFile('migrations/' + name, 'utf8')).split('\n')) {
    statement += line + '\n';
    if (line.trim().endsWith(';') && (!/CREATE TRIGGER/i.test(statement) || line.trim() === 'END;')) {
      await db.prepare(statement).run();
      statement = '';
    }
  }
  if (statement.trim()) throw new Error('Incomplete migration: ' + name);
}
