-- Script to update round robin table with system data

-- Temporary table to hold information on new cursor
create temporary table newcursor (
  like aqfeather.rrcursor);

insert into newcursor (rrcid, rrid, updatetime)
select 1::int as rrcid,
       case when t1.cursorpoint > 14400::int then t1.cursorpoint - 14400::int
       else t1.cursorpoint
       end as rrid,
       transaction_timestamp() as updatetime
  from (select round((extract(epoch from transaction_timestamp())
		      - extract(epoch from t2.updatetime))::numeric / 10.0)::int
		 + t2.rrid as cursorpoint
	  from aqfeather.rrcursor t2) t1;


-- Update the table with the new json
create temporary table newjson (
  rrid int,
  datastring jsonb);

\copy newjson (datastring) from /tmp/air-quality-data.json with (FORMAT text, HEADER false);

update newjson as t1
   set rrid = (select t2.rrid from newcursor t2)
       from newjson
 where newjson.datastring is not null;

 select * from newcursor;

update aqfeather.rrdata as t1
   set datastring = newjson.datastring
       from newjson
 where t1.rrid = newjson.rrid;

-- Any spots between current update and previous update will be NULL
update aqfeather.rrdata as t1
   set datastring = NULL
 where (t1.rrid < (select rrid from newcursor)
	and t1.rrid > (select rrid from aqfeather.rrcursor)) or
       ((select rrid from aqfeather.rrcursor) > (select rrid from newcursor)
       and (t1.rrid < (select rrid from newcursor)
	    or t1.rrid > (select rrid from aqfeather.rrcursor)));

-- Update the cursor table
update aqfeather.rrcursor as t1
   set (rrid, updatetime) = (
     select rrid, updatetime
       from newcursor t2
      where t1.rrcid = t2.rrcid);
