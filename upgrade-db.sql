

insert into public.update_request(id, pdb_id, created, version, user_id)
	select id, pdb_id, request_date, version, user_id from public.update_request_v1;

update public_update_request()