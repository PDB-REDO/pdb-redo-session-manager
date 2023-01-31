--
-- PostgreSQL database dump
--

-- Dumped from database version 14.6 (Ubuntu 14.6-0ubuntu0.22.04.1)
-- Dumped by pg_dump version 14.6 (Ubuntu 14.6-0ubuntu0.22.04.1)

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: auth_user; Type: TABLE; Schema: public; Owner: pdbAdmin
--

CREATE TABLE public.auth_user (
    id bigint NOT NULL,
    accepts_privacy_policy boolean,
    account_created timestamp without time zone,
    account_updated timestamp without time zone,
    email_address character varying(255) NOT NULL,
    institution character varying(255),
    last_login timestamp without time zone,
    last_submitted_job_date timestamp without time zone,
    last_submitted_job_id bigint,
    last_submitted_job_status integer,
    name character varying(255) NOT NULL,
    password character varying(255) NOT NULL,
    last_update_request timestamp without time zone
);


ALTER TABLE public.auth_user OWNER TO "pdbAdmin";

--
-- Name: cart; Type: TABLE; Schema: public; Owner: pdbAdmin
--

CREATE TABLE public.cart (
    id bigint NOT NULL,
    created_date timestamp without time zone,
    status integer,
    user_id bigint
);


ALTER TABLE public.cart OWNER TO "pdbAdmin";

--
-- Name: cart_item; Type: TABLE; Schema: public; Owner: pdbAdmin
--

CREATE TABLE public.cart_item (
    id bigint NOT NULL,
    file character varying(255),
    cart_id bigint
);


ALTER TABLE public.cart_item OWNER TO "pdbAdmin";

--
-- Name: hibernate_sequence; Type: SEQUENCE; Schema: public; Owner: pdbAdmin
--

CREATE SEQUENCE public.hibernate_sequence
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.hibernate_sequence OWNER TO "pdbAdmin";

--
-- Name: session; Type: TABLE; Schema: public; Owner: pdbAdmin
--

CREATE TABLE public.session (
    id integer NOT NULL,
    user_id bigint,
    name character varying NOT NULL,
    token character varying NOT NULL,
    realm character varying,
    created timestamp with time zone DEFAULT CURRENT_TIMESTAMP NOT NULL,
    expires timestamp with time zone DEFAULT (CURRENT_TIMESTAMP + '1 year'::interval) NOT NULL
);


ALTER TABLE public.session OWNER TO "pdbAdmin";

--
-- Name: session_id_seq; Type: SEQUENCE; Schema: public; Owner: pdbAdmin
--

CREATE SEQUENCE public.session_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.session_id_seq OWNER TO "pdbAdmin";

--
-- Name: session_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: pdbAdmin
--

ALTER SEQUENCE public.session_id_seq OWNED BY public.session.id;


--
-- Name: update_request; Type: TABLE; Schema: public; Owner: pdbAdmin
--

CREATE TABLE public.update_request (
    id bigint NOT NULL,
    pdb_id character varying(255),
    request_date timestamp without time zone,
    version double precision,
    user_id bigint
);


ALTER TABLE public.update_request OWNER TO "pdbAdmin";

--
-- Name: session id; Type: DEFAULT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.session ALTER COLUMN id SET DEFAULT nextval('public.session_id_seq'::regclass);


--
-- Name: auth_user auth_user_pkey; Type: CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.auth_user
    ADD CONSTRAINT auth_user_pkey PRIMARY KEY (id);


--
-- Name: cart_item cart_item_pkey; Type: CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.cart_item
    ADD CONSTRAINT cart_item_pkey PRIMARY KEY (id);


--
-- Name: cart cart_pkey; Type: CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.cart
    ADD CONSTRAINT cart_pkey PRIMARY KEY (id);


--
-- Name: session session_pkey; Type: CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.session
    ADD CONSTRAINT session_pkey PRIMARY KEY (id);


--
-- Name: auth_user uk_4snrvai6dys4tn4nl4v64k30w; Type: CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.auth_user
    ADD CONSTRAINT uk_4snrvai6dys4tn4nl4v64k30w UNIQUE (name);


--
-- Name: auth_user uk_enonvvphwn0p9l3xdhkd14hq0; Type: CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.auth_user
    ADD CONSTRAINT uk_enonvvphwn0p9l3xdhkd14hq0 UNIQUE (email_address);


--
-- Name: update_request update_request_pkey; Type: CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.update_request
    ADD CONSTRAINT update_request_pkey PRIMARY KEY (id);


--
-- Name: cart_item fk1uobyhgl1wvgt1jpccia8xxs3; Type: FK CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.cart_item
    ADD CONSTRAINT fk1uobyhgl1wvgt1jpccia8xxs3 FOREIGN KEY (cart_id) REFERENCES public.cart(id);


--
-- Name: update_request fkj1xcofajdd5dfvy7kwpsy95mi; Type: FK CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.update_request
    ADD CONSTRAINT fkj1xcofajdd5dfvy7kwpsy95mi FOREIGN KEY (user_id) REFERENCES public.auth_user(id);


--
-- Name: cart fklhignvpyis23in1soqpk73ldk; Type: FK CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.cart
    ADD CONSTRAINT fklhignvpyis23in1soqpk73ldk FOREIGN KEY (user_id) REFERENCES public.auth_user(id);


--
-- Name: session session_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: pdbAdmin
--

ALTER TABLE ONLY public.session
    ADD CONSTRAINT session_user_id_fkey FOREIGN KEY (user_id) REFERENCES public.auth_user(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED;


--
-- PostgreSQL database dump complete
--

