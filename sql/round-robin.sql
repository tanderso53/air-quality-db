-- Creates round robin data infrastructure to store system data in

--drop schema aqfeather cascade;
create schema if not exists aqfeather;
grant usage on schema aqfeather to PUBLIC;
alter default privileges in schema aqfeather
  GRANT SELECT ON TABLES TO grafana_select;

-- Create round robin table for raw json data
create table if not exists aqfeather.rrdata (
  rrid int unique primary key,
  datastring jsonb);

insert into aqfeather.rrdata
select s.a::int as rrid, NULL::jsonb as datastring
  from generate_series(1::int, 14400::int, 1::int) as s(a)
	 on conflict do nothing;

-- Create cursor table
create table if not exists aqfeather.rrcursor (
  rrcid int unique primary key,
  rrid int not null,
  updatetime timestamptz not null);

-- Insert sample values into cursor table
insert into aqfeather.rrcursor (rrcid, rrid, updatetime)
values (1::int, 1::int, transaction_timestamp())
       on conflict do nothing;

-- Create unrolled view of table
create or replace view aqfeather.rawjson as
  select t2.updatetime + (t1.rrid - t2.rrid - 14400::int) * '10 sec'::interval
	   as updatetime,
	 t1.datastring
    from aqfeather.rrdata t1, aqfeather.rrcursor t2
   where t1.rrid > t2.rrid and t1.datastring is not null
	 union
  select t2.updatetime - (t2.rrid - t1.rrid) * '10 sec'::interval
	   as updatetime,
	 t1.datastring
    from aqfeather.rrdata t1, aqfeather.rrcursor t2
   where t1.rrid <= t2.rrid and t1.datastring is not null
   order by updatetime;
