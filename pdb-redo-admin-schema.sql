-- New DB Schema for pdb-redo
DROP TABLE IF EXISTS public.session;
DROP TABLE IF EXISTS public.user;

CREATE TABLE public.user (
	id serial primary key,
	name varchar NOT NULL UNIQUE,
	institution varchar NOT NULL,
	email varchar NOT NULL,
	password varchar NOT NULL,
	created timestamp with time zone default CURRENT_TIMESTAMP NOT NULL,
	modified timestamp with time zone default CURRENT_TIMESTAMP NOT NULL,
	last_job_nr int default 0,
	last_job_date timestamp with time zone,
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
	ON public.user FOR EACH ROW EXECUTE PROCEDURE update_modified_column();

CREATE TABLE public.session (
	id serial primary key,
	user_id bigint references public.user on delete cascade deferrable initially deferred,
	name varchar NOT NULL,
	token varchar NOT NULL,
	created timestamp with time zone default CURRENT_TIMESTAMP not null,
	expires timestamp with time zone default CURRENT_TIMESTAMP + interval '1 year' not null
);

-- CREATE TABLE public.job {
-- 	id serial primary key,
-- 	user_id bigint references public.user on delete cascade deferrable initially deferred,
-- 	job_nr int not null,
--     created timestamp with time zone default CURRENT_TIMESTAMP not null,
-- 	exit_status int
-- }
ALTER TABLE
	public.user OWNER TO "pdbAdmin";

ALTER TABLE
	public.session OWNER TO "pdbAdmin";

insert into
	public.user (name, institution, email, password, created)
values
	(
		'maarten',
		'NKI',
		'maarten@hekkelman.net',
		'!NMqfP62tAQY/HNZYSAJZNBEFVJ3Uo/eY0AeI4Z9SudYh3jz5WUxvMPGYBV55Pb1F',
		'2018-11-06 21:06:31.673'
	)