-- Annotation query for Air Quality Status events
drop view if exists aqfeather.events; -- cascade;
create or replace view aqfeather.events as
  with q2 as (
    select q1.updatetime, q1.state, q1.status, q1.active,
	   (case when first_value(q1.active)
		 over (partition by q1.state order by q1.updatetime
		       rows between 1 preceding and current row) < q1.active
	     then true else false
	    end)::boolean as is_rising_edge,
	   (case when first_value(q1.active)
		 over (partition by q1.state order by q1.updatetime
		       rows between 1 preceding and current row) > q1.active
	     then true else false
	    end)::boolean as is_falling_edge
      from (
	select t1.updatetime, t2.key as state, (t1.datastring ->> 'status')::bigint as status,
	       ((t2.value::bigint &
		 (t1.datastring ->> 'status')::bigint) > 0::bigint)::integer as active
	  from aqfeather.rawjson t1, jsonb_each_text(t1.datastring -> 'status masks') t2
	 where datastring -> 'status masks' is not null
	 order by t1.updatetime) q1)
  select q3.state, q3.updatetime as start_time,
	 min(q4.updatetime) as end_time,
	 '0x'::text || to_hex(q3.status)::text as status
    from (
      select q2.state, q2.active, q2.updatetime, q2.status
	from q2
       where q2.is_rising_edge) q3
	 left join (
	   select q2.state, q2.active, q2.updatetime
	     from q2
	    where q2.is_falling_edge) q4
	     on q3.state = q4.state and q4.updatetime > q3.updatetime
   group by q3.state, q3.updatetime, q3.status
   order by q3.updatetime;

grant select on aqfeather.events to grafana_select;
