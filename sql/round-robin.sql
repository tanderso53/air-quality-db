-- Creates round robin data infrastructure to store system data in

--drop schema aqfeather cascade;
create schema if not exists aqfeather;
grant usage on schema aqfeather to PUBLIC;
alter default privileges in schema aqfeather
  GRANT SELECT ON TABLES TO grafana_select;

-- Create round robin table for raw json data
create table if not exists aqfeather.rrdata (
  rrid int,
  devid int,
  datastring jsonb);

-- insert into aqfeather.rrdata
-- select s.a::int as rrid, NULL::jsonb as datastring
--   from generate_series(1::int, 14400::int, 1::int) as s(a)
-- 	 on conflict do nothing;

-- Create cursor table
create table if not exists aqfeather.rrcursor (
  rrcid int unique primary key,
  rrid int not null,
  devid int,
  updatetime timestamptz not null);

-- Create metadata table
create table if not exists aqfeather.dev_metadata (
  dmid int unique primary key,
  devid int unique,
  numrows int not null,
  measinterval interval not null);

-- Create device table
create table if not exists aqfeather.devices (
  devid int unique primary key,
  serialnum text,
  devname text);

-- Insert sample values into cursor table
-- insert into aqfeather.rrcursor (rrcid, rrid, updatetime)
-- values (1::int, 1::int, transaction_timestamp())
--        on conflict do nothing;

-- Create unrolled view of table
create or replace view aqfeather.rawjson as
  select t2.updatetime + (t1.rrid - t2.rrid - t3.numrows) * t3.measinterval
	   as updatetime, t1.datastring, t4.devname
    from aqfeather.rrdata t1, aqfeather.rrcursor t2, aqfeather.dev_metadata t3,
	 aqfeather.devices t4
   where t1.rrid > t2.rrid and t1.datastring is not null
     and t1.devid = t2.devid and t1.devid = t3.devid and t1.devid = t4.devid
   union
  select t2.updatetime - (t2.rrid - t1.rrid) * '10 sec'::interval
	   as updatetime, t1.datastring, t4.devname
    from aqfeather.rrdata t1, aqfeather.rrcursor t2, aqfeather.dev_metadata t3,
	 aqfeather.devices t4
   where t1.rrid <= t2.rrid and t1.datastring is not null
     and t1.devid = t2.devid and t1.devid = t3.devid and t1.devid = t4.devid
   order by devname, updatetime;
