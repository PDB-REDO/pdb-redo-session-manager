-- 

DROP TABLE IF EXISTS public.run;
DROP TABLE IF EXISTS public.session;

CREATE TABLE public.session (
	id serial primary key,
	user_id bigint references public.auth_user on delete cascade deferrable initially deferred,
    name varchar NOT NULL,
	token varchar NOT NULL,
    created timestamp with time zone default CURRENT_TIMESTAMP not null,
	expires timestamp with time zone default CURRENT_TIMESTAMP + interval '1 year' not null
);

ALTER TABLE public.session OWNER TO "pdbRedoAdmin";

-- CREATE TYPE run_status AS ENUM('undefined', 'registered', 'starting', 'queued', 'running', 'stopping', 'stopped', 'ended', 'deleting');

-- CREATE TABLE public.run (
-- 	id serial primary key,
-- 	user_id bigint references public.auth_user on delete cascade deferrable initially deferred,
-- 	created timestamp with time zone default CURRENT_TIMESTAMP not null,
-- 	session_id integer references public.session on delete cascade deferrable initially deferred,
-- 	status run_status
-- );

-- ALTER TABLE public.run OWNER TO "pdbRedoAdmin";

-- INSERT INTO public.run(id, user_id, created, status)
-- 	SELECT last_submitted_job_id, id, last_submitted_job_date,
-- 		CASE WHEN last_submitted_job_status = 0 THEN 'undefined'::run_status
-- 			 WHEN last_submitted_job_status = 0 THEN 'registered'::run_status
-- 			 WHEN last_submitted_job_status = 0 THEN 'starting'::run_status
-- 			 WHEN last_submitted_job_status = 0 THEN 'queued'::run_status
-- 			 WHEN last_submitted_job_status = 0 THEN 'running'::run_status
-- 			 WHEN last_submitted_job_status = 0 THEN 'stopping'::run_status
-- 			 WHEN last_submitted_job_status = 0 THEN 'stopped'::run_status
-- 			 WHEN last_submitted_job_status = 0 THEN 'ended'::run_status
-- 			 WHEN last_submitted_job_status = 0 THEN 'deleting'::run_status
-- 		END
-- 	  FROM auth_user
-- 	 WHERE last_submitted_job_id NOT NULL;
