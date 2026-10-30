PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE events (
                        id INTEGER PRIMARY KEY,
                        type VARCHAR NOT NULL,
                        time DATETIME NOT NULL,
                        end_time DATETIME,
                        details VARCHAR NOT NULL,
                        creation_time DATETIME DEFAULT (strftime('%Y-%m-%d %H:%M:%fZ', 'now')) NOT NULL,
                        read_time DATETIME
);

CREATE INDEX ix_events_time ON events (time);
CREATE INDEX ix_events_type ON events (type);
CREATE INDEX ix_events_id ON events (id);
CREATE INDEX ix_events_end_time ON events (end_time);
COMMIT;

