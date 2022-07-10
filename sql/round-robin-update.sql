-- Script to update round robin table with system data

-- Update the table with the new json
create temporary table newjson (
  rrid int,
  datastring jsonb,
  devid int,
  devname text);

\copy newjson (datastring, devname) from pstdin with (FORMAT text, HEADER false);

update newjson as t1
   set devid = aqfeather.devices.devid
       from aqfeather.devices
 where aqfeather.devices.devname = t1.devname;

-- Temporary table to hold information on new cursor
create temporary table newcursor (
  like aqfeather.rrcursor);

insert into newcursor (rrcid, rrid, updatetime, devid)
select t1.rrcid,
       case when t1.cursorpoint > t1.numrows then t1.cursorpoint - t1.numrows
       else t1.cursorpoint
       end as rrid,
       transaction_timestamp() as updatetime, t1.devid
  from (select t2.rrcid,
	       round((extract(epoch from transaction_timestamp())
		      - extract(epoch from t2.updatetime))::numeric / 10.0)::int
		 + t2.rrid as cursorpoint, t3.numrows, t3.measinterval, t2.devid
	  from aqfeather.rrcursor t2, aqfeather.dev_metadata t3
	 where t2.devid = t3.devid) t1, newjson t2
 where t1.devid in (t2.devid);

update newjson as t1
   set rrid = newcursor.rrid
       from newcursor
 where t1.datastring is not null and newcursor.devid = t1.devid;

select * from newcursor;

update aqfeather.rrdata as t1
   set datastring = newjson.datastring
       from newjson
 where t1.rrid = newjson.rrid and t1.devid = newjson.devid;

-- Any spots between current update and previous update will be NULL
update aqfeather.rrdata as t1
   set datastring = NULL
       from (select t2.rrid as new_rrid, t3.rrid as old_rrid, t2.devid
	       from newcursor t2, aqfeather.rrcursor t3, newjson t4
	      where t2.rrcid = t3.rrcid
		and t2.devid in (t4.devid)) q1
 where ((t1.rrid < q1.new_rrid
	 and t1.rrid > q1.old_rrid)
	 or (q1.old_rrid > q1.new_rrid
	     and (t1.rrid < q1.new_rrid
		  or t1.rrid > q1.old_rrid)))
   and t1.devid = q1.devid;

-- Update the cursor table
update aqfeather.rrcursor as t1
   set rrid = q1.rrid, updatetime = q1.updatetime
       from (select t2.rrcid, t2.rrid, t2.updatetime
	       from newcursor t2, newjson t3
	      where t2.devid in (t3.devid)) q1
 where t1.rrcid = q1.rrcid;
