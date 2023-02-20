-- New DB Schema for pdb-redo

CREATE SCHEMA IF NOT EXISTS redo AUTHORIZATION "pdbAdmin";

DROP TABLE IF EXISTS redo.token;
DROP TABLE IF EXISTS redo.update_request;
DROP TABLE IF EXISTS redo.user;

CREATE TABLE redo.user (
	id serial primary key,
	name varchar NOT NULL UNIQUE,
	institution varchar NOT NULL,
	email varchar NOT NULL,
	password varchar NOT NULL,
	created timestamp with time zone default CURRENT_TIMESTAMP NOT NULL,
	modified timestamp with time zone default CURRENT_TIMESTAMP NOT NULL,
	last_login timestamp with time zone,
	last_job_nr int default 0,
	last_job_date timestamp with time zone,
	last_job_status varchar,
	last_token_nr int default 0,
	UNIQUE(name, email)
);

CREATE
OR REPLACE FUNCTION update_modified_column() RETURNS TRIGGER AS $$
BEGIN NEW.modified = now();
	RETURN NEW;
END;
$$ language 'plpgsql';

CREATE TRIGGER update_user_modtime BEFORE
UPDATE OF name, institution, email, password
	ON redo.user FOR EACH ROW EXECUTE PROCEDURE update_modified_column();

CREATE TABLE redo.token (
	id serial primary key,
	user_id bigint references redo.user on delete cascade deferrable initially deferred,
	name varchar NOT NULL,
	secret varchar NOT NULL,
	created timestamp with time zone default CURRENT_TIMESTAMP not null,
	expires timestamp with time zone default CURRENT_TIMESTAMP + interval '1 year' not null
);

CREATE TABLE redo.update_request (
	id serial primary key,
	pdb_id varchar(8) not null,
	created timestamp with time zone default CURRENT_TIMESTAMP not null,
	version double precision not null,
	user_id bigint references redo.user on delete cascade deferrable initially deferred,
	UNIQUE(pdb_id, user_id)
);

ALTER TABLE
	redo.user OWNER TO "pdbAdmin";

ALTER TABLE
	redo.token OWNER TO "pdbAdmin";

ALTER TABLE
	redo.update_request OWNER TO "pdbAdmin";

insert into
	redo.user (name, institution, email, password, created)
values
	(
		'maarten',
		'NKI',
		'maarten@hekkelman.net',
		'!NMqfP62tAQY/HNZYSAJZNBEFVJ3Uo/eY0AeI4Z9SudYh3jz5WUxvMPGYBV55Pb1F',
		'2018-11-06 21:06:31.673'
	)