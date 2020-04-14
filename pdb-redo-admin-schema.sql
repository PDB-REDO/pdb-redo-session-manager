-- 

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
